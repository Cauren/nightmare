#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <cstring>
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

static uint_t kmalloc(uint_t bytes)
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
    if(pfree+nfree != max) {
	pfree = max;
	nfree = 0;
    }
    if(nfree<wants) {
	if((pfree+wants)/page > CPU::mem_alloc_)
	    return 0;
	heap.resize(pfree+wants);
    }
    for(p=pfree+1; p<pfree+wants; p++)
	heap[p] = -pfree;
    heap[pfree] = wants;
    return pfree*page;
}

void CPU::oscall(void)
{
    ccr&C = false;

    auto uaddr = [this](const AReg& reg, size_t len = 0, bool writes = false) -> Addr {
	Segment* s = seg(reg.seg);
	if(!s || reg.addr+len > s->len)
	    throw EFAULT;
	if(len && !(writes? s->write: s->read))
	    throw EACCES;
	if(s->super && !(smr&SU))
	    throw EPERM;
	return {*s, reg.addr};
    };

    auto ustring = [this,&uaddr](const AReg& reg) -> std::string {
	Addr s = uaddr(reg, 1);
	std::string str;
	while(s.addr < s.seg->len) {
	    byte_t c = unsigned_<9>(s.seg->mem[s.addr++]);
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
	    if(uint_t addr = kmalloc((d[1].data+page-1) / page)) {
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
			Addr segdata(*seg(0777777), segmap+16*s.value);
			if((segdata+int_t(4)).ulong())
			    throw ENOMEM;
			uint_t mem = kmalloc(s.size);
			if(!mem)
			    throw ENOMEM;
			segdata.ulong(mem);
			segdata.ulong(s.size);
			segdata.uword(s.value==3? 001: 060);
			segdata.uword(0);
			segdata.ulong(0);
			for(const auto& d: s.data)
			    memcpy(mem_+mem+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));
		    }
		} else
		    throw ENOEXEC;
	    }
	}
    } catch(int e) {
	d[0].data = signed_<36>(e);
	ccr&C = true;
    }
}



}; // namespace Nightmare

