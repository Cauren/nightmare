#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <cstring>
#include <ncursesw/curses.h>
#include "machine.hh"
#include "cpu.hh"
#include "object.hh"

namespace Nightmare {


// Ugly hack for kmalloc/kfree:
// we manage the heap "outside" of the CPU in a really naive way
// by just looking for the first free set of consecutive 4k pages 
// with a dumb linear search.  Good enough for now.
//
// If len<0 then this is part of allocation for page -len
// If len>0 then this is a len page allocation
// if len==0 then the page is free
static std::vector<int64_t> heap = { 2, 1 }; // first two pages are never available
static constexpr size_t page = 4*1024;

uint_t Machine::kmalloc(uint_t bytes)
{
    size_t  wants = (bytes+page-1) / page;
    size_t  pfree = 0;
    size_t  nfree = 0;
    size_t  max = heap.size();

    size_t  p = 1;

    while(p<max && nfree<wants) {
	if(heap[p] > 0) {
	    p += heap[p];
	    nfree = 0;
	    continue;
	}
	pfree = p;
	while(p<max && heap[p]==0)
	    p++, nfree++;
    }
    if(max<1)
	max = pfree = 1;
    if(pfree+nfree != max) {
	pfree = max;
	nfree = 0;
    }
    if(nfree<wants) {
	if((pfree+wants) > mem_alloc/page)
	    return 0;
	heap.resize(pfree+wants);
    }
    for(p=pfree+1; p<pfree+wants; p++)
	heap[p] = -pfree;
    heap[pfree] = wants;
    return pfree*page;
}

void Machine::output(byte_t ch)
{
    char    c[2];
    if(ch > 127) {
	c[0] = 0xC0|(ch>>6);
	c[1] = 0x80|(ch&63);
	write(1, c, 2);
    }
    else {
	c[0] = ch;
	write(1, c, 1);
    }
}

byte_t Machine::input(void)
{
    char    ch;
    if(read(0, &ch, 1) == 1) {
	return (unsigned char)(ch);
    }
    return -1;
}


void CPU::oscall(void)
{
    ccr&C = false;

    auto uaddr = [this](const AReg& reg, size_t len = 0, bool writes = false) -> Addr {
	CSeg* s = seg(reg.seg);
	if(!s || reg.addr+len > s->len)
	    throw EFAULT;
	if(len && !(s->flags & (writes? Segment::WRITE: Segment::READ)))
	    throw EACCES;
	if((s->flags & Segment::SUPER) && !(smr&SU))
	    throw EPERM;
	return {s, reg.addr};
    };

    auto ustring = [this,&uaddr](const AReg& reg) -> std::string {
	Addr s = uaddr(reg, 1);
	std::string str;
	while(s.addr < s.seg->len) {
	    byte_t c = s.seg->mem[s.addr++].ub();
	    if(!c)
		return str;
	    if(c<128)
		str.append(1, char(c));
	    else {
		str.append(1, char(0xC0|(c>>6)));
		str.append(1, char(0x80|(c&63)));
	    }
	}
	throw EFAULT;
    };

    try {
	switch(d[0].data) {

	  case 0: // kmalloc
	    if(!(smr&SU))
		throw EPERM;
	    if(uint_t addr = mach.kmalloc((d[1].data+page-1) / page)) {
		a[0].seg = 0777777;
		a[0].addr = addr;
	    } else
		throw ENOMEM;
	    return;

	  case 1: // kfree
	    if(!(smr&SU))
		throw EPERM;
	    break;

	  case 2: // load
	    if(!(smr&SU))
		throw EPERM;
	    if(!segmap || segmap_len<5) {
		throw ENOMEM;
	    } else {
		std::string fname = ustring(a[0]);
		std::ifstream file("./fs/" + fname);
		if(file.bad()) {
		    throw errno;
		}
		std::unique_ptr<Object> nobj;
		Object* obj = debug? debug: (nobj = std::make_unique<Object>()).get();
		if(obj->load(file)) {
		    for(const auto& s: obj->segs) {
			if(s.size <= 0)
			    continue;
			if(s.value >= segmap_len)
			    throw ENOMEM;
			Segment&    seg = MemPtr(mach.mem).ref<Segment>(segmap, s.value);

			if(seg.flags&Segment::VALID || seg.size)
			    throw ENOMEM;
			uint_t mem = mach.kmalloc(s.size);
			if(!mem)
			    throw ENOMEM;
			seg.base = mem;
			seg.size = s.size;
			seg.flags = s.value==3? 001: 006;
			seg.spare_ = 0;
			seg.segid = 0;
			for(const auto& d: s.data)
			    memcpy(mach.mem+mem+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));
		    }
		} else
		    throw ENOEXEC;
	    }
	    break;

	  case 3:
	    {
		Addr s = uaddr(a[0], 1);
		std::string str;
		while(s.addr < s.seg->len) {
		    byte_t c = s.seg->mem[s.addr++].ub();
		    if(!c)
			return;
		    mach.output(c);
		}
	    }
	    throw EFAULT;

	  case 4:
	    mach.output(unsigned_<9>(d[1].data));
	    break;

	  case 5:

#ifdef DEBUG

	    refresh();

#endif

	    d[0].data = signed_<9>(mach.input());
	    break;

	}
    } catch(int e) {
	d[0].data = signed_<36>(e);
	ccr&C = true;
    }
}



}; // namespace Nightmare

