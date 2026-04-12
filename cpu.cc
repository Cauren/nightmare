#include "cpu.hh"


namespace Nightmare {


CPU::Addr CPU::addr(uword_t segno, uint_t a, bool super)
{
    Segment& s = scache[segno&15];
    super |= smr & SM::Super;

    if(!s.valid || s.seg!=segno) {
	switch(segno) {
	  case 0777777:
	    s = { segno, true, true, true, true, true, mem_alloc_, mem_ };
	    break;
	  case 0777776:
	    s = { segno, true, true, true, true, false, 6*32, mem_+segtable };
	    break;
	  default: {
	    Addr segdata = addr(0777776, segno*16, super);
	    segdata.reads(16);
	    uint_t sa = segdata.ulong();
	    uint_t sl = segdata.ulong();
	    uword_t sf = segdata.uword();
	    s = { segno, true, bool(sf&010), bool(sf&004), bool(sf&002), bool(sf&001), sl, mem_+sa };
	    break;
	  }
	}
    }
    if(s.super && !super)
	throw Fault{ EPERM, Addr(s, a) };
    return Addr(s, a);
}

bool CPU::reset(void)
{
    smr = 0;
    smr += SM::Super;
    ir = 0777;
    ccr = 0;

    for(int i=0; i<16; i++)
	scache[i].valid = false;

    try {
	Addr rvec = addr(0777777, 2, true); // We ignore segno for the reset vector
	rvec.reads(4);
	pc.seg = 0777777;
	pc.addr = rvec.ulong();
    } catch(const Fault&) {
	return true;
    }
    return false;
}

void CPU::trap(byte_t num, const AReg& faddr)
{
    try {
	AReg usp = a[7];
	SM osmr = smr;

	if(!(smr & SM::Super)) {
	    a[7] = ssp;
	    smr += SM::Super;
	}

	Addr frame = addr(a[7]);

	frame.writes(24);
	frame.uword(uword_t(ccr));
	frame.uword(uword_t(ir));
	frame.uword(uword_t(osmr));
	frame.areg(faddr);
	frame.areg(usp);
	frame.areg(pc);

	a[7].addr += 24;
	ir = 0777;

	Addr vec = addr(0777777, num*6);
	vec.reads(6);
	uword_t vs = vec.uword();
	pc = AReg{ vs, vec.ulong() };

    } catch(const Fault&) {

	throw Fault{ ELOOP, faddr };

    }
}

#if 0
void CPU::rte(void)
{
    a[7].addr -= 24;
    Addr frame = addr(a[7]);

    frame.reads(24);
    ccr = frames.uword();
    ir = frames.uword();
    SM nsmr = frames.uword();
    frame += 6; // skip fault
    AReg usp = frame.areg();
    pc = frame.areg();

    if(!(nsmr & SM::Super)) {
	ssp = a[7];
	a[7] = usp;
    }
    smr = nsmr;
}
#endif

void CPU::run(void)
{
    bool halted = false;
    while(!halted) try {

	if(pending) {
	    int tr;
	    for(tr=0; tr<24; tr++)
		if(pending & (1l << tr))
		    break;
	    if(tr < 24) {
		pending ^= (1l << tr);
		trap(tr, fault);
		continue;
	    }
	    pending = 0;
	}

	Addr	instr = addr(pc);
	uword_t	ilen = 2;

	instr.execs(2);
	uword_t	opcode = instr.uword();
	uword_t ext = 0;
	uword_t	ereg;
	uint_t	uinput;
	int_t	sinput;
	uword_t	ea_bits;
	uword_t	easz = 2;
	int_t	offset = 0;
	int_t	disp = 0;
	int_t	index = 0;
	Addr	eaddr;
	bool	memea = true;
	enum {
	    Immed, DReg,
	    Absolute,
	    PostInc, PreDec, Indirect, PreIndex, PostIndex
	}	eamode;

	if(opcode & (1<<17)) {
	    // bit 17 set means there are EA fields on the opcode

	    eamode = Indirect;
	    if((opcode&0600000)==0400000 || (opcode&0760000)==0600000) // those have eam field
		easz = (opcode>>6) & 3;

	    switch(easz) {
	      case 3:
		memea = false;
		uinput = opcode&077;
		ea_bits = 6;
		eamode = Immed;
		break;
	      case 0: ea_bits = 9; break;
	      case 1: ea_bits = 18; break;
	      default: ea_bits = 36; break;
	    }

	    if(eamode != Immed) {
		ereg = opcode & 007;
		switch(opcode & 070) {
		  case 000: // DR
		    memea = false;
		    eamode = DReg;
		    uinput = unsigned_(ea_bits, d[ereg].data);
		    break;
		  case 010: // (ar)
		    break;
		  case 020: // (ar)+
		    eamode = PostInc;
		    break;
		  case 030: // -(ar)
		    eamode = PreDec;
		    break;
		  case 040: // (d18,ar)
		    instr.execs(2);
		    disp = instr.sword();
		    break;
		  case 050: // ar:d18
		    eamode = Absolute;
		    instr.execs(2);
		    eaddr = addr(a[ereg].seg, instr.uword());
		    break;
		  case 070:
		    if((opcode&077) == 071) {
			memea = false;
			eamode = Immed;
			if(easz==2) {
			    instr.execs(4);
			    uinput = instr.ulong();
			} else {
			    instr.execs(2);
			    uinput = instr.uword();
			}
			break;
		    } else if((opcode&077) == 072) {
			eamode = Absolute;
			instr.execs(6);
			eaddr = addr(instr.uword(), instr.ulong());
			break;
		    } else if((opcode&077) != 070) {
			throw Fault{ EINVAL, pc };
		    }
		    ereg = 8;
		    // fallthrough
		  case 060:
		    instr.execs(2);
		    ext = instr.uword();
		    if(ext & (1<<17)) // has index
			index = sex_<36>(d[(ext>>9) & 7].data) * (1 << ((ext>>12)&3));
		    if((ext>>15) > 1) { // has offset
			if(ext & (1<<14)) {
			    instr.execs(2);
			    offset = instr.sword();
			}
			disp = sex_<9>(ext & 0777);
		    } else
			offset = sex_<9>(ext & 0777);
		    if(ext & (1<<15)) // memory indirect
			eamode = ((ext>>15) == 7)? PostIndex: PreIndex;
		    break;
		}
	    }

	    if(memea && eamode != Absolute) {
		if(ereg < 8) {
		    if(eamode == PreDec)
			a[ereg].addr -= (1<<easz);
		    eaddr = addr(a[ereg]);
		    if(eamode == PostInc)
			a[ereg].addr += (1<<easz);
		} else
		    eaddr = addr(pc);
		eaddr += offset;
		if(eamode == PostIndex) {
		    disp += index;
		} else
		    eaddr += index;
		if(eamode == PostIndex || eamode == PreIndex) {
		    eaddr.reads(6);
		    uword_t seg = eaddr.uword();
		    eaddr = addr(seg, eaddr.ulong());
		}
		eaddr += disp;
	    }

	}

	static auto ea_read = [&](void) {
	    Addr rea = eaddr;
	    if(memea) {
		switch(easz) {
		  case 0:
		    rea.reads(1);
		    uinput = rea.ubyte();
		    sinput = signed_<9>(uinput);
		    break;
		  case 1:
		    rea.reads(2);
		    uinput = rea.uword();
		    sinput = signed_<18>(uinput);
		    break;
		  default:
		    rea.reads(4);
		    uinput = rea.ulong();
		    sinput = signed_<36>(uinput);
		    break;
		}
	    } else
		sinput = signed_(ea_bits, uinput);
	};

	static auto ea_uwrite = [&](int_t n) {
	    if(memea) switch(easz) {
	      case 0:
		eaddr.writes(1);
		eaddr.ubyte(n);
		break;
	      case 1:
		eaddr.writes(2);
		eaddr.uword(n);
		break;
	      default:
		eaddr.writes(4);
		eaddr.ulong(n);
		break;
	    } else if(eamode == DReg)
		d[ereg].data = unsigned_<36>(n);
	    else
		throw Fault{ EINVAL, pc };
	};

	static auto ea_swrite = [&](int_t n) {
	    if(memea) switch(easz) {
	      case 0:
		eaddr.writes(1);
		eaddr.sbyte(n);
		break;
	      case 1:
		eaddr.writes(2);
		eaddr.sword(n);
		break;
	      default:
		eaddr.writes(4);
		eaddr.slong(n);
		break;
	    } else if(eamode == DReg)
		d[ereg].data = signed_<36>(n);
	    else
		throw Fault{ EINVAL, pc };
	};

	bool jump = false;
	// decode instructions here

	if(!jump) {
	    pc.addr = instr.addr;
	};

    } catch(const Fault& f) {
	if(f.trap == ELOOP) // Double fault.  Give up.
	    halted = true;
	else {
	    pending |= 1l << int(f.trap);
	    fault = f.fault;
	}
    }
}

} // namespace Nightmare




int main(int, char**) {
    Nightmare::CPU	cpu;

    cpu.mem_ = new Nightmare::byte_t[cpu.mem_alloc_ = 640*1024]; // 640K ought to be enough for anyone.  :-)
    if(!cpu.reset())
	cpu.run();
}

