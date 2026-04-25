#include <fstream>
#include <sstream>
#include <vector>
#include <memory>
#include <cstring>
#include <map>
#include <vector>
#include <ncursesw/curses.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

// NOTE: see the note in open() below about those
#include <linux/openat2.h>
#include <sys/syscall.h>

// NOTE: see the note in load() below about this
#include <ext/stdio_filebuf.h>

#include "machine.hh"
#include "cpu.hh"
#include "object.hh"


namespace Nightmare {


    static constexpr uword_t SEG_PER_PROC = 32;

    // Our "OS" keeps most of its data structures outside the actual virtual
    // memory, both because simpler and because this allows us the much more
    // efficient native data structures.
    //
    // The exceptions to this are the segment table (because the _CPU_ needs
    // to see those in memory) and a few global kernel variables because they
    // are needed for the bootstrap process.

    struct Process {
	enum Status: uword_t {
	    Ready, Sleep, Dead
	};

	CPU::Segment			segtable[SEG_PER_PROC];

	UWord				pid;
	UWord				ppid;
	UWord				status;
	ULong				sleep_on;
	SegAddr				ssp;
    };
    std::map<uword_t, uint_t>	procs;
    uword_t			npid = 2;

    typedef decltype(stat::st_ino) Inode;

    struct Segment {
	size_t				refs;
	uint_t				base;
	uint_t				size;
	Inode				ino = 0;
	uword_t				flags = 0;
    };
    std::vector<Segment> segs;	    // global segment list

    static long rootfd = -1;

    long open(std::string path, decltype(open_how::flags) flags = O_PATH, decltype(open_how::mode) mode = 0)
    {

	// XXX: This is very Linux-specific right now as it uses the safe
	// openat2 system call which we use to carefully contain the VM to
	// the rootfd in a way a "mere" chroot() cannot guarantee.
	//
	// It might be ported (with care) to a different OS with a combination
	// of other system calls, or by very carefully resolving the path
	// component-by-component with judicious use of openat().

	if(rootfd < 0) {
	    rootfd = ::open("fs/", O_PATH);
	}

	struct open_how how;
	memset(&how, 0, sizeof(how));

	how.flags = flags;
	how.mode = mode;
	how.resolve = RESOLVE_IN_ROOT|RESOLVE_NO_MAGICLINKS|RESOLVE_NO_XDEV;

	long fd = syscall(SYS_openat2, rootfd, path.c_str(), &how, sizeof(how));
	return fd;
    }

    struct LoadedObject {
	size_t				refs = 0;
	std::unique_ptr<Object>		obj;
	std::map<uword_t, uint_t>	segs;

					LoadedObject(void) = default;
					LoadedObject(LoadedObject&& o) {
					    refs = o.refs;
					    o.refs = 0;
					    obj = std::move(o.obj);
					    segs = std::move(o.segs);
					};
    };

    static std::map<Inode, LoadedObject>	loaded_objects;

    uint_t Machine::salloc(uint_t size)
    {
	uint_t	base = kmalloc(size);
	if(!base)
	    throw ENOMEM;
	auto& seg = segs.emplace_back(Segment{0, base, size});
	return &seg - segs.data();
    }

    void Machine::sfree(uint_t sid)
    {
	if(sid >= segs.size())
	    return;
	Segment& s = segs[sid];
	if(s.ino) {
	    auto i = loaded_objects.find(s.ino);
	    if(!--i->second.refs)
		loaded_objects.erase(i);
	    s.ino = 0;
	}
	kfree(s.base);
	s.size = 0;
	s.base = 0;
    }

    CPU::Segment& CPU::mmap(uint_t segid, uword_t segno)
    {
	if(!segmap || segno>=segmap_len || segid>=segs.size())
	    throw ENOMEM;
	Segment& seg = MemPtr(mach.mem).ref<Segment>(segmap, segno);
	if(seg.flags & Segment::VALID) {
	    if(segno==4 || ((seg.flags & Segment::SUPER) && !smr&SU))
		throw EPERM;
	    munmap(segno);
	}
	Nightmare::Segment& ns = Nightmare::segs[segid];
	ns.refs++;
	seg.base = ns.base;
	seg.size = ns.size;
	seg.flags = ns.flags | Segment::VALID;
	seg.segid = segid;
	invalidate();
	return seg;
    }

    void CPU::munmap(uword_t segno)
    {
	if(!segmap || segno>=segmap_len)
	    throw ENOMEM;

	Segment& seg = MemPtr(mach.mem).ref<Segment>(segmap, segno);

	if(!(seg.flags & Segment::VALID))
	    return;

	Nightmare::Segment& ns = Nightmare::segs[seg.segid];
	if(!--ns.refs)
	    mach.sfree(seg.segid);

	seg.size = 0;
	seg.flags = 0;
	invalidate();
    }

    void CPU::freesegs(bool everything)
    {
	if(!segmap)
	    return;
	for(int i=0; i<segmap_len; i++)
	    if(everything || i!=4)
		munmap(i);
    }

    LoadedObject& load(Machine& mach, std::string path)
    {
	long fd = open(path, O_RDONLY);
	if(fd < 0)
	    throw errno;

	struct stat sb;
	if(fstat(fd, &sb))
	    throw errno;

	if((sb.st_mode&S_IFMT != S_IFREG))
	    throw ENOEXEC;

	Inode ino = sb.st_ino;
	if(auto lo = loaded_objects.find(ino); lo != loaded_objects.end())
	    return lo->second;

	std::unique_ptr<Object> obj = std::make_unique<Object>();

	// NOTE: c++ doesn't understand POSIX file descriptors in any stantard way
	// so we have to use a platform-specific hack here

	// This one for GCC
	__gnu_cxx::stdio_filebuf<char> filebuf(fd, std::ios_base::in);
	std::istream file(&filebuf);

	if(obj->load(file)) {

	    auto nlo = loaded_objects.emplace(ino, LoadedObject());
	    LoadedObject& lo = nlo.first->second;

	    for(const auto& s: obj->segs) {
		if(s.size <= 0)
		    continue;

		uint_t segid = mach.salloc(s.size);

		segs[segid].ino = ino;
		segs[segid].flags = s.flags | CPU::Segment::VALID;

		for(const auto& d: s.data)
		    memcpy(mach.mem+segs[segid].base+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));

		lo.segs.emplace(s.value, segid);
		lo.refs++;
	    }

	    lo.obj = std::move(obj);
	    return lo;

	}

	throw ENOEXEC;
    }

    // We manage the memory space in a really naive way by just looking for the first
    // free set of consecutive 4k pages with a dumb linear search.  Good enough
    // for the actual use case of a toy VM.
    //
    // If len<0 then this is part of allocation for page -len
    // If len>0 then this is a len page allocation
    // if len==0 then the page is free

    static std::vector<int64_t> heap = { 1 }; // first page is never available
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

    bool Machine::kfree(uint_t addr)
    {
	size_t	page = addr/page;

	if(!page)
	    return true;

	size_t	size = heap[page];
	if(size<0)
	    return true;

	while(size--)
	    heap[page++] = 0;

	return false;
    }

    void Machine::output(byte_t ch)
    {
	char    c[2];
	if(ch > 127) {
	    c[0] = 0xC0|(ch>>6);
	    c[1] = 0x80|(ch&63);
	    write(stdout, c, 2);
	}
	else {
	    c[0] = ch;
	    write(stdout, c, 1);
	}
    }

    byte_t Machine::input(void)
    {
	char    ch;
#ifdef DEBUG
	refresh();
#endif
	if(read(stdin, &ch, 1) == 1) {
	    return (unsigned char)(ch);
	}
	return -1;
    }


    // Thing to know while in kernel space:
    //	* (once userspace exists) segment 4 always points to the Process structure and is therefore
    //	  the matching userspace context.  The SMA register will point at the segtable there.

    // System calls follow the Call ABI convention:
    //	* up to the first two non-address arguments are in d0 and d1
    //	* up to the first two address arguments are in a0 and a1
    //	* return argument goes in d0 or a0 as apropriate, d1 and a1 are not guaranteed preserved
    // In addition,
    //	* if there is an error C is set and the errno is in d0
    
    void CPU::savectx(MemPtr ssp_addr)
    {
	Process& self = MemPtr(mach.mem+segmap);

	ContextFrame& cf = ssp_addr;

	cf.ccr = ccr;
	cf.ir = ir;
	cf.smr = smr;
	cf.fault = nullptr;
	cf.usp = a[7];
	cf.pc = pc;
	for(int i=0; i<8; i++)
	    cf.d[i] = d[i];
	for(int i=0; i<7; i++)
	    cf.a[i] = a[i];

	ssp.addr += sizeof(ContextFrame);

	self.ssp = ssp;
    }

    void CPU::loadctx(uint_t uarea)
    {
	Process& self = MemPtr(mach.mem+uarea);

	ssp = self.ssp;
	ssp.addr -= sizeof(ContextFrame);

	ContextFrame& cf = MemPtr(mach.mem+uarea+ssp.addr);

	ccr = cf.ccr;
	ir = cf.ir;
	smr = cf.smr;
	a[7] = cf.usp;
	pc = cf.pc;
	for(int i=0; i<8; i++)
	    d[i] = cf.d[i];
	for(int i=0; i<7; i++)
	    a[i] = cf.a[i];

	segmap = uarea;
	segmap_len = SEG_PER_PROC;
    }

    void CPU::oscall(void)
    {
	ccr&C = false;

	auto uaddr = [this](const AReg& reg, size_t len = 0, int flags = 0) -> MemPtr {
	    CSeg* s = seg(reg.seg);
	    if(!s || !(s->flags&Segment::VALID) || (reg.addr >= s->len) || (len && (reg.addr+len > s->len)) )
		throw EFAULT;
	    if((s->flags & Segment::SUPER) && !(smr&SU))
		throw EPERM;
	    if(len && (s->flags&flags) != flags)
		throw EACCES;
	    return { s->mem + reg.addr };
	};

	auto ustring = [this,&uaddr](const AReg& reg) -> std::string {
	    CSeg* s = seg(reg.seg);
	    uint_t addr = reg.addr;

	    if(!s || !(s->flags&Segment::VALID) || (addr >= s->len))
		throw EFAULT;
	    if((s->flags & Segment::SUPER) && !(smr&SU))
		throw EPERM;
	    if(!(s->flags & Segment::READ))
		throw EACCES;
	    std::string str;
	    while(addr < s->len) {
		byte_t c = s->mem[addr++].ub();
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

	      case 0: // raw kmalloc
		if(!(smr&SU))
		    throw EPERM;
		if(uint_t addr = mach.kmalloc(d[1].data)) {
		    a[0].seg = 0777777;
		    a[0].addr = addr;
		} else
		    throw ENOMEM;
		return;

	      case 1: // raw kfree
		if(!(smr&SU))
		    throw EPERM;
		break;

	      case 2: // exec
		if(!segmap || segmap_len<5) {
		    throw ENOMEM;
		} else {
		    std::string fname = ustring(a[0]);

		    freesegs();
		    auto& stackseg = mmap(mach.salloc(4096), 0);
		    stackseg.flags = Segment::READ|Segment::WRITE;

		    LoadedObject& lo = load(mach, fname);
		    debug = lo.obj.get();
		    for(const auto& seg: lo.segs) {
			mmap(seg.second, seg.first);
		    }

		    // link so in.  someday.
		    // relocate.  eventually.

		    if(smr&SU) {
			a[0].seg = lo.obj->sseg;
			a[0].addr = lo.obj->saddr;
		    } else {
			a[7].seg = 0;
			a[7].addr = 0;
			pc.seg = lo.obj->sseg;
			pc.addr = lo.obj->saddr;
		    }
		}
		break;

	      case 4:
		mach.output(unsigned_<9>(d[1].data));
		break;

	      case 5:

		d[0].data = signed_<9>(mach.input());
		break;

	      case 6: // fork

		{
		    uint_t  useg = mach.salloc(4096);
		    Nightmare::Segment& ns = Nightmare::segs[useg];
		    Process& np = MemPtr(mach.mem+ns.base);

		    np.segtable[4].base = ns.base;
		    np.segtable[4].size = ns.size;
		    np.segtable[4].flags = Segment::SUPER | Segment::READ | Segment::WRITE;
		    np.segtable[4].segid = useg;
		    ns.refs = 1;

		    np.ssp.seg = 4;
		    np.ssp.addr = 3*1024;

		    if(smr&SU) {

			// Special case: if we fork() from the kernel we aren't actually
			// forking, we're creating the initial context for the first
			// userspace process, and returning an absolute pointer to that.

			for(int i=0; i<SEG_PER_PROC; i++)
			    if(i != 4)
				np.segtable[i].flags = 0;

			a[0].seg = 0777777;
			a[0].addr = ns.base;

			np.pid = 1;
			np.ppid = 1;
			np.status = Process::Ready;

			procs.emplace(1, ns.base);
			invalidate();

		    } else {

			Process& self = MemPtr(mach.mem+segmap);
			for(int i=0; i<SEG_PER_PROC; i++)
			    if(i != 4) {
				np.segtable[i] = self.segtable[i];
				if(self.segtable[i].flags & Segment::VALID) {
				    if(self.segtable[i].flags & Segment::WRITE)
					np.segtable[i].flags = (self.segtable[i].flags & ~Segment::WRITE) | Segment::COW;
				    Nightmare::segs[np.segtable[i].segid].refs++;
				}
			    }

			np.ppid = self.pid;
			do {
			    npid = unsigned_<18>(npid);
			    if(!npid)
				npid = 2;
			} while(!procs.emplace((np.pid = npid++), ns.base).second);

			d[0].data = np.pid;
			savectx(mem(ssp, true));

			d[0].data = 0;
			segmap = ns.base;
			invalidate();
		    }

		}

		break;

	    }

	} catch(int e) {
	    d[0].data = signed_<36>(e);
	    ccr&C = true;
	}
    }



}; // namespace Nightmare

