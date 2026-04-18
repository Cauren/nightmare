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
    if(max<1)
	max = pfree = 1;
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

wchar_t	    CPU::screen_[16][64];
int	    CPU::scr_x_ = 0;
int	    CPU::scr_y_ = 0;

void emit(byte_t ch)
{
    static int esc = 0;
    static int row;

    if(ch == 27) {
	esc = 1;
	return;
    }
    if(esc == 2) {
	row = ch-31;
	esc = 3;
	return;
    }
    if(esc == 3) {
	esc = 0;
	int col = ch-31;
	col = (col<0)? 0: (col>63)? 63: 0;
	row = (row<0)? 0: (row>15)? 15: 0;
	CPU::scr_x_ = col;
	CPU::scr_y_ = row;
	return;
    }
    bool scrup = false;
    bool scrdn = false;
    if(esc && ch>31) {
	switch(ch) {
	  case 'A': if(CPU::scr_y_) CPU::scr_y_--; break;
	  case 'B': if(CPU::scr_y_<15) CPU::scr_y_++; break;
	  case 'C': if(CPU::scr_x_<63) CPU::scr_x_++; break;
	  case 'D': if(CPU::scr_x_) CPU::scr_x_--; break;
	  case 'H': CPU::scr_x_ = CPU::scr_y_ = 0; break;
	  case 'I': if(CPU::scr_y_) CPU::scr_y_--; else scrdn = true; break;
	  case 'Y': esc = 2; return;
	  case 'J': for(int y=CPU::scr_y_+1; y<16; y++) memset(CPU::screen_[y], 0, 64*sizeof(byte_t)); break;
	  case 'K': for(int x=CPU::scr_x_; x<64; x++) CPU::screen_[CPU::scr_y_][x] = 0; break;
	}
	esc = 0;
    } else {
	esc = 0;
	switch(ch) {
	  case 8: if(CPU::scr_x_) CPU::scr_x_--; break;
	  case 10: if(CPU::scr_y_<15) CPU::scr_y_++; else scrup = true; break;
	  case 13: CPU::scr_x_ = 0; break;
	  default:
	    if(ch>31) {
		CPU::screen_[CPU::scr_y_][CPU::scr_x_++] = ch;
		if(CPU::scr_x_ > 63) {
		    CPU::scr_x_ = 0;
		    if(CPU::scr_y_ < 15)
			CPU::scr_y_++;
		    else
			scrup = true;
		}
	    }
	    break;
	}
    }
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
			segdata.uword(s.value==3? 001: 006);
			segdata.uword(0);
			segdata.ulong(0);
			for(const auto& d: s.data)
			    memcpy(mem_+mem+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));
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
		    byte_t c = unsigned_<9>(s.seg->mem[s.addr++]);
		    if(!c)
			return;
		    emit(c);
		}
	    }
	    throw EFAULT;
	}
    } catch(int e) {
	d[0].data = signed_<36>(e);
	ccr&C = true;
    }
}



}; // namespace Nightmare

