#include "cpu.hh"


#define IMATCH(mask,bits) if((i&(0b##mask##UL))==(0b##bits##UL))

namespace Nightmare {



void CPU::reset(void)
{
    sm.super = true;
    sm.reset = true;
    sm.restart = false;
    sm.signal = false;
    ir = 0777;
    ccr = 0;

    trap = 0;
    segs[0].seg = 0;
    segs[0].valid = true;
    segs[0].super = true;
    segs[0].read = true;
    segs[0].write = false;
    segs[0].exec = true;
    segs[0].addr = 0;
    segs[0].len = m.mem_alloc;
    for(int i=1; i<16; i++)
	segs[i].valid = false;

    pc.seg = 0;
    pc.addr = 2;
    Byte* rst_addr = mem(pc, 4);
    if(!rst_addr)
	halt();
    pc.addr = read4(rst_addr);
}

void CPU::trap(uint16_t num, const AReg& ea)
{
    Byte* frame = mem(sm.super? a[7]: ssp, 22, true);
    if(!frame)
	halt();
    write2(frame+20, pc.seg);
    write4(frame+16, pc.addr);
    write2(frame+14, a[7].seg);
    write4(frame+10, a[7].addr);
    write2(frame+8,  ea.seg);
    write2(frame+4,  ea.addr);
    write1(frame+3,  smr);
    write1(frame+2,  ir);
    write1(frame+1,  ccr);
    ssp.addr += 22;
    ir = 0777;
    if(!sm.super) {
	a[7] = ssp;
	sm.super = true;
    }
    pc.seg = 1;
    pc.addr = num*6;
    Byte* trap_addr = mem(pc, 6);
    if(trap_addr) {
	pc.seg = read2(trap_addr);
	pc.addr = read4(trap_addr+2);
	return;
    }
    if(num == 1)
	halt();
    throw Trap(1, pc);
}

void CPU::rte(void)
{
    if(!sm.super)
	throw Trap(3, pc);
    a[7].addr -= 22;
    Byte* frame = mem(a[7], 22);
    if(!frame) {
	a[7].addr += 22;
	throw Trap(1, pc);
    }
    read1(frame+1,  ccr);
    read1(frame+2,  ir);
    read1(frame+3,  smr);
    if(!sm.super) { // return to userspace
	ssp = a[7];
	read2(frame+14, a[7].seg);
	read4(frame+10, a[7].addr);
    }
    read2(frame+20, pc.seg);
    read4(frame+16, pc.addr);
}

void CPU::run(void)
{
    halted = false;
    while(!halted) {
	try {
	    if(!pending) {
		// check for interrupts here
	    }
	    if(pending) {
		uint16_t tn = pending;
		pending = 0;
		trap(tn, fault);
	    }

	    Segment& s = seg(pc);
	    int ilen = 2;
	    int imax = s.len-pc.addr;
	    if(!s.exec || (s.super && !sm.super) || ilen>imax)
		throw Trap(2, pc+ilen);

	    Byte*	instr = pc.addr + m.mem;
	    uint16_t	i = read2(instr);
	    AReg	ea;
	    int		ireg = (i>>9) & 7;
	    int64_t	idata;
	    int		isize = 0;
	    bool	predec = false;
	    bool	postinc = false;
	    int		areg; // for postdec and preinc
	    bool	earead = false;
	    bool	eawrite = false;

	    enum {
		IMPLIED, IMMED, DR, EA,
	    }		imode = IMPLIED;

	    if((i>>17) & 1) { // Instructions with EA fields

		int eam = (i>>6) & 3;
		static const int eam_bits[4] = { 9, 18, 36, 6 };
		isize = eam_bits[eam]; // default
		if(eam == 3) {
		    imode = IMMED;
		    idata = i & 077;
		} else
		    imode = EA;

		/*  */ IMATCH( 110'000'000'100'000'000, 100'000'000'000'000'000 ) { // op2, ea->dr
			earead = true;
		} else IMATCH( 110'000'000'100'000'000, 100'000'000'100'000'000 ) { // op2, dr->ea
		    eawrite = true;
		} else IMATCH( 111'110'000'100'000'000, 110'000'000'000'000'000 ) { // op1, ea->ea
		    eawrite = earead = true;
		} else IMATCH( 111'111'000'100'000'000, 110'001'000'000'000'000 ) { // lda, ea->ar
		    if(eam==0)
			throw Trap(3, pc);
		    earead = true;
		} else IMATCH( 111'111'000'100'000'000, 110'001'000'100'000'000 ) { // sta, ar->ea
		    if(eam==0)
			throw Trap(3, pc);
		    eawrite = true;
		} else
		    throw Trap(3, pc);

		ea = a[areg = (i&7)]; // by default
		if(earead || eawrite) switch((i>>3 & 7)) {
		    case 0:
			imode = DR;
			break;
		    case 1:
			break;
		    case 2:
			postinc = true;
			break;
		    case 3:
			ea.addr -= isize/9;
			predec = true;
			break;
		    case 4:
			ilen += 2;
			if(ilen > imax)
			    throw Trap(2, pc+ilen);
			ea.addr += sex2(read2(instr+2));
			break;
		    case 5:
			ilen += 4;
			if(ilen > imax)
			    throw Trap(2, pc+ilen);
			ea = AReg(a[i&7].seg, read4(instr+2));
			break;
		    case 7:
			if((i&7) == 1) { // immediate
			    imode = IMMED;
			    if(isize > 18) {
				ilen += 4;
				if(ilen > imax)
				    throw Trap(2, pc+ilen);
				idata = read4(instr+2);
				isize = 36;
			    } else {
				ilen += 2;
				if(ilen > imax)
				    throw Trap(2, pc+ilen);
				idata = read2(instr+2);
				isize = 18;
			    }
			}
			if(i&7) // not PC-relative extended
			    throw Trap(3, pc);
			ea = pc;
			// fall through
		    case 6:
			ilen += 2;
			if(ilen > imax)
			    throw Trap(2, pc+ilen);
			else {
			    uint32_t	ext = read2(instr+2);
			    int64_t	disp = sex1(ext & 0777);
			    int64_t	idx = 0;
			    int64_t	offset = 0;

			    ext >>= 9;

			    if(ext & 0600) { // has offset
				if(ext & 0040) {
				    ilen += 4;
				    if(ilen > imax)
					throw Trap(2, pc+ilen);
				    offset = sex4(read4(instr+4));
				} else {
				    ilen += 1;
				    if(ilen > imax)
					throw Trap(2, pc+ilen);
				    offset = sex2(read2(instr+4));
				}
			    } else {
				offset = disp;
				disp = 0;
			    }

			    if(ext & 0400) { // has index
				idx = d[ext&7];
				idx *= 1 << ((ext>>3)&3);
			    }

			    if((ext & 0700) == 0700) { // post indirect
				offset += idx;
				idx = 0;
			    }

			    ea.addr += offset;
			    if(ext & 0100) { // is indirect
				Byte* indir = mem(ea, 6);
				if(!indir)
				    throw Trap(2, ea);
				ea.seg = read2(indir2);
				ea.addr = read4(indir+2);
			    }

			    ea.addr += idx;
			    ea.addr += disp;
			}
			break;
		}

	    if(earead) switch(imode) {
		case IMPLIED: // not supposed to be possible
		    throw Trap(3, pc);
		case DR:
		    idata = d[ireg].n;
		    break;
		case EA:
		    Byte* src = mem(ea, isize/9);
		    if(!src)
			throw Trap(2, ea);
		    if(isize >= 36)
			idata = read4(src);
		    else if(isize >= 18)
			idata = read2(src);
		    else
			idata = read1(src);
		    break;
	    }

	    auto sidata = [&idata, &isize]() -> int64_t {
		int64_t sign = 1 << (isize-1);

		if(idata & sign) {
		    if(idata != sign)
			return sign-idata;
		    if(sm.signal)
			throw Trap(4, (imode==EA)? ea: pc);
		    return sign;
		}
		return idata;
	    };

	    auto result = [&isize, &imode](int64_t n) -> void {
		cc.c =   n & ~((1<<36) - 1);
		cc.z = !(n &= ((1<<36) - 1));
		switch(imode) {
		    case DR:
			d[ireg].n = n;
			break;
		    case EA:
			Byte* dst = mem(ea, isize/9);
			if(!dst)
			    throw Trap(2, ea);
			if(isize >= 36)
			    write4(dst, idata);
			else if(isize >= 18)
			    write2(dst, idata);
			else
			    write1(dst, idata);
		    default:
			throw Trap(3, pc);
		}
	    };

	    auto sresult = [&isize, &imode](int64_t n) -> void {
		int64_t sign = 1 << (isize-1);
		cc.v = false;
		if(n<0) {
		    cc.n = true;
		    n = -n;
		    if(n >= sign) {
			n = sign;
			cc.v = true;
		    }
		    n |= sign;
		} else if(n >= sign) {
		    cc.n = false;
		    n = sign;
		    cc.v = true;
		}
		result(n);
	    };

	    /*  */ IMATCH( 
	    } else IMATCH( 
	    } else
		throw Trap(3, pc);

	    // write results
	    if(eawrite) switch(imode) {
		case DR:
		    d[ireg].n = idata;
		    break;
		case EA:
		    Byte* d = mem(ea, isize, true);
		    if(isize > 36)
			write4(d, idata);
		    else if(isize > 18)
			write2(d, idata);
		    else
			write1(d, idata);
		    break;
		default:
		    throw Trap(3, pc);
	    }

	    // everything worked, fixup
	    if(predec)
		a[areg].n -= isize/9;
	    if(postinc)
		a[areg].n += isize/9;
	    pc.addr += ilen;


	} catch(const Trap& t) {
	    pending = t.num;
	    fault = t.ea;
	} catch(const Halt&) {
	    halted = true;
	}
    }
}

const Segment& CPU::seg(const AReg& ea)
{
    Segment& s = segs[ea.seg&15];
    if(!s.valid || s.seg!=ea.seg) {
	AReg	sptr;
	str.seg = 2;
	sptr.addr = ea.seg<<4;
	Byte*	seg_addr = mem(sptr, 16);
	if(!seg_addr)
	    throw Trap(2, ea);
	s.seg = ea.seg;
	s.valid = true;
	s.addr = read4(seg_addr);
	s.len = read4(seg_addr+4);
	uint32_t flags = read2(seg_addr+8);
	s.super = flags&0001;
	s.read  = flags&0002;
	s.write = flags&0004;
	s.exec  = flags&0010;

    }
    return s;
}

Byte* CPU::mem(const AReg& ea, size_t len, bool write)
{
    const Segment& s = seg(ea);
    if(	   (write? s.write: s.read)
	&& (sm.super || !s.super)
	&& (ea.addr+len < s.len) )
	return m.mem + ea.addr;
    return 0;
}


} // namespace Nightmare
