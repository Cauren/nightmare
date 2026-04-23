#define _XOPEN_SOURCE_EXTENDED

#include <string>
#include <format>
#include <cerrno>
#include <cstring>
#include <fstream>
#include <pty.h>
#include "machine.hh"
#include "cpu.hh"
#include "object.hh"

namespace Nightmare {

template<typename BIT>
Nightmare::CPU::Bitreg<BIT>::Bits operator | (BIT b1, BIT b2)
{
    return typename Nightmare::CPU::Bitreg<BIT>::Bits(b1) | b2;
};

CPU::CSeg CPU::sixseven;

void CPU::invalidate(void)
{
    sixseven.seg = 0777777;
    sixseven.mem = mach.mem;
    sixseven.len = mach.mem_alloc;
    sixseven.flags = Segment::VALID|Segment::SUPER|Segment::READ|Segment::WRITE|Segment::EXEC;
    for(int i=0; i<16; i++)
	scache[i].flags = 0;
}

CPU::CSeg* CPU::seg(uword_t segno)
{
    if(segno == 0777777)	// bypasses the cache entirely
	return &sixseven;

    CSeg* s = scache+(segno&15);

    if(!(s->flags&Segment::VALID) || s->seg!=segno) {
	if(!segmap || segno>=segmap_len) {
	    s->seg = segno;
	    s->flags = 0;
	    throw Fault{ eFAULT, Addr(s, 0) };
	}
	const Segment& sd = MemPtr(mach.mem).ref<Segment>(segmap, segno);
	*s = { segno, sd.flags, sd.size, mach.mem + sd.base };
    }

    return s;
}

CPU::Addr CPU::addr(uword_t segno, uint_t a, bool super)
{
    CSeg* s = seg(segno);

    super |= smr&SU;
    if(!s || a > s->len)
	throw Fault{ eFAULT, Addr(s, a) };
    if((s->flags&Segment::SUPER) && !super)
	throw Fault{ ePERM, Addr(s, a) };
    return Addr(s, a);
}

bool CPU::reset(void)
{
    smr = SU | RST;
    ccr = 0;
    ir = 0777;

    for(int i=0; i<8; i++) {
	d[i].data = 0;
	a[i].seg = 0;
	a[i].addr = 0;
    }
    segmap = 0;
    segmap_len = 0;

    invalidate();

    try {
	Addr rvec = addr(0777777, 2, true); // We ignore segno for the reset vector
	rvec.reads(4);
	pc.seg = 0777777;
	pc.addr = rvec->ul();
    } catch(const Fault&) {
	return true;
    }
    return false;
}

bool doforth=false;

void CPU::trap(byte_t num, const AReg& faddr)
{
    if(num == 23) { // TRAP #15
	oscall();
	return;
    }
    if(num == 22) { // TRAP #14
	dodebug = debug!=nullptr;
	return;
    }

    try {
	AReg usp = a[7];
	auto osmr = smr;

	if(!(smr & SU)) {
	    a[7] = ssp;
	    smr += SU;
	}

	Addr fa = addr(a[7]);
	fa.writes(24);

	auto& frame = (ExceptionFrame&)fa;
	frame.ccr = ccr;
	frame.ir = ir;
	frame.smr = osmr;
	frame.fault = faddr;
	frame.usp = usp;
	frame.pc = pc;

	a[7].addr += 24;
	ir = 0777;

	Addr vec = addr(0777777, num*6);
	vec.reads(6);
	pc = (SegAddr&)vec;

    } catch(const Fault&) {

	throw Fault{ eLOOP, faddr };

    }
}

struct opmask_ {
    uword_t	mask;
    uword_t	bits;
};

static constexpr opmask_ operator ""_m(const char* d, size_t)
{
    opmask_ om = { 0, 0 };
    int left=18;
    char ch;
    while(left && (ch=*d++)) switch(ch) {
      case '0':
      case '1':
	om.mask = (om.mask<<1) | 1;
	om.bits = (om.bits<<1) | (ch&1);
	--left;
	break;
      case 'x':
	om.mask <<= 1;
	om.bits <<= 1;
	--left;
	break;
    }
    om.mask <<= left;
    om.bits <<= left;
    return om;
}

static constexpr bool operator == (const opmask_& m, uword_t o) { return (o&m.mask)==m.bits; };
static constexpr bool operator == (uword_t o, const opmask_& m) { return (o&m.mask)==m.bits; };

void CPU::run(void)
{
    bool    halted = false;
    Addr    instr;

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

	instr = addr(pc);
	uword_t	ilen = 2;

	instr.execs(2);
	uword_t	opcode = instr->uw();
	instr += 2;

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
	    None,
	    Immed, DReg,
	    Absolute,
	    PostInc, PreDec, Indirect, PreIndex, PostIndex
	}	eamode;
	static const char* const    eamode_name[] = {
	    "None", "Immediate", "DReg",
	    "Absolute", "PostInc", "PreDec", "Indirect", "PreIndex", "PostIndex"
	};

#ifdef DEBUG

	auto display_insn_decode = [&](void) -> void {
	    mvaddstr(0, 46, "Decoded EA: "); addstr(eamode_name[int(eamode)]);
	    switch(eamode) {
	      case Immed:
		break;
	      case DReg:
		wprintw(stdscr, " (d%d)", int(ereg));
		break;
	      case None:
		break;
	      default:
		wprintw(stdscr, " (%o:%lo)", int(eaddr.seg->seg), (unsigned long)(eaddr.addr));
		break;
	    }
	    clrtoeol();
	    move(1,46);
	    if(doforth) {
		addstr("Stack: ");
		for(int i=0; i<a[6].addr; i+=4) {
		    Addr si = addr(a[6].seg, i);
		    wprintw(stdscr, "%ld ", si->sl());
		}
		if(a[6].addr)
		    wprintw(stdscr, "| %ld", sex_<36>(d[7].data));
	    }
	    clrtoeol();
	};

	auto display_cpu = [&](void) -> void {
	    mvaddstr(0, 0, "┏━━━┯━━━━━━━━━━━━━┳━━━┯━━━━━━━━━━━━━━━━━━━━┓");
	    clrtoeol();
	    for(int i=0; i<8; i++) {
		std::string ln = std::format("┃ d{}│{:12o} ┃ a{}│{:>6o}:{:012o} ┃",
					     i, signed_<36>(d[i].data),
					     i, unsigned_<18>(a[i].seg), unsigned_<36>(a[i].addr));
		mvaddstr(i+1, 0, ln.c_str());
		clrtoeol();
	    }
	    mvaddstr(9, 0, "┗━━━┷━━━━━━━━━━━━━┻━━━┷━━━━━━━━━━━━━━━━━━━━┛");
	    clrtoeol();

	    auto ppc = debug->slines.upper_bound(Object::SourceLine{ pc.addr, pc.seg });
	    int dln = 1;
	    while(dln>-2 && ppc!=debug->slines.begin()) {
		if(std::prev(ppc)->seg != pc.seg)
		    break;
		ppc--;
		dln--;
	    }
	    move(10, 0);
	    clrtoeol();
	    mvaddstr(11, 0, std::format("PC: {:06o}:{:012o}", pc.seg, pc.addr).c_str());
	    move(12, 0); clrtoeol();
	    move(13, 0); clrtoeol();
	    for(int ln=-3; ln<10; ln++) {
		if(ln >= 0) {
		    move(ln+14, 0);
		    int l = ppc->len;
		    CSeg* s = seg(ppc->seg);
		    if(s) {
			MemPtr mem = s->mem + ppc->addr;
			if(l > 8)
			    l = 6;
			int i;
			for(i=0; i<l; i+=2)
			    addstr(std::format("{:06o} ", mem[i].uw()).c_str());
			if(i < l)
			    addstr("...");
		    }
		}
		clrtoeol();
		move(ln+14, 29);
		if(ln<dln || ppc==debug->slines.end() || ppc->seg!=pc.seg) {
		    addstr(" ┊");
		} else {
		    if(ln==0) {
			addstr((ppc->addr==pc.addr)? "──→": "~~↴");
		    } else
			addstr(" ┋ ");
		    addstr(ppc->text.c_str());
		    ppc++;
		}
		clrtoeol();
	    }

	};

#endif

	insn++;

	eamode = None;
	if((opcode>>16)&3) {
	    // bit 17 or 16 set means there are EA fields on the opcode

	    eamode = Indirect;
	    if(opcode == "1"_m || opcode == "010'00x'xxx'0"_m) // those have eam field
		easz = (opcode>>6) & 3;

	    switch(easz) {
	      case 3:
		memea = false;
		sinput = sex_<6>(opcode&077);
		uinput = signed_<36>(sinput);
		ea_bits = 36;
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
		    disp = instr->sw();
		    instr += 2;
		    break;
		  case 050: // ar:d18
		    eamode = Absolute;
		    instr.execs(2);
		    eaddr = addr(a[ereg].seg, instr->uw());
		    instr += 2;
		    break;
		  case 070:
		    if((opcode&077) == 071) {
			memea = false;
			eamode = Immed;
			if(easz==2) {
			    instr.execs(4);
			    uinput = instr->ul();
			    instr += 4;
			} else {
			    instr.execs(2);
			    uinput = instr->uw();
			    instr += 2;
			}
			break;
		    } else if((opcode&077) == 072) {
			eamode = Absolute;
			instr.execs(6);
			eaddr = addr(instr[0].uw(), instr[2].ul());
			instr += 6;
			break;
		    } else if((opcode&077) != 070) {
			throw Fault{ eINVAL, pc };
		    }
		    ereg = 8;
		    // fallthrough
		  case 060:
		    instr.execs(2);
		    ext = instr->uw();
		    instr += 2;
		    if(ext & (1<<17)) // has index
			index = sex_<36>(d[(ext>>9) & 7].data) * (1 << ((ext>>12)&3));
		    if((ext>>15) > 1) { // has offset
			if(ext & (1<<14)) {
			    instr.execs(2);
			    offset = instr->sw();
			    instr += 2;
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
		    eaddr = addr(a[ereg]);
		    if(eamode == PreDec)
			eaddr -= (1<<easz);
		} else
		    eaddr = addr(pc);
		eaddr += offset;
		if(eamode == PreIndex) {
		    disp += index;
		} else
		    eaddr += index;
		if(eamode == PostIndex || eamode == PreIndex) {
		    eaddr.reads(6);
		    uword_t seg = eaddr[0].uw();
		    eaddr = addr(seg, eaddr[2].ul());
		}
		eaddr += disp;
	    }

	}

#ifdef DEBUG

	if(dodebug && debug) {
	    display_cpu();
	    display_insn_decode();
	    char c = getch();
	    if(c == 'b')
		__asm__("int $3");
	    if(c == 'c')
		dodebug = false;
	    if(c == 'f')
		doforth = true;
	    if(c == 'q') {
		break;
	    }
	}

#endif

	if(memea && eamode != Absolute) {
	    if(ereg < 8) {
		if(eamode == PreDec)
		    a[ereg].addr -= (1<<easz);
		if(eamode == PostInc)
		    a[ereg].addr += (1<<easz);
	    }
	}

	static auto ea_readjust = [&](uword_t sz) -> void {
	    if(eamode==PreDec) {
		a[ereg].addr -= sz - (1<<easz);
		eaddr.addr = a[ereg].addr;
	    } else if(eamode==PostInc)
		a[ereg].addr += sz - (1<<easz);
	};

	static auto ea_read = [&](void) -> void {
	    Addr rea = eaddr;
	    if(memea) {
		switch(easz) {
		  case 0:
		    rea.reads(1);
		    uinput = rea->ub();
		    sinput = sex_<9>(uinput);
		    break;
		  case 1:
		    rea.reads(2);
		    uinput = rea->uw();
		    sinput = sex_<18>(uinput);
		    break;
		  default:
		    rea.reads(4);
		    uinput = rea->ul();
		    sinput = sex_<36>(uinput);
		    break;
		}
	    } else {
		sinput = sex_(ea_bits, uinput);
		//uinput = signed_<36>(sinput);
	    }
	};

	static auto ea_uwrite = [&](int_t n) -> void {
	    if(eamode == DReg) {
		utest<36>(n);
		d[ereg].data = unsigned_<36>(n);
	    } else if(memea) switch(easz) {
	      case 0:
		utest<9>(n);
		eaddr.writes(1);
		eaddr->ub(n);
		break;
	      case 1:
		utest<18>(n);
		eaddr.writes(2);
		eaddr->uw(n);
		break;
	      default:
		utest<36>(n);
		eaddr.writes(4);
		eaddr->ul(n);
		break;
	    } else
		throw Fault{ eINVAL, pc };
	};

	static auto ea_swrite = [&](int_t n) -> void {
	    if(eamode == DReg) {
		stest<36>(n);
		d[ereg].data = signed_<36>(n);
	    } else if(memea) switch(easz) {
	      case 0:
		stest<9>(n);
		eaddr.writes(1);
		eaddr->sb(n);
		break;
	      case 1:
		stest<18>(n);
		eaddr.writes(2);
		eaddr->sw(n);
		break;
	      default:
		stest<36>(n);
		eaddr.writes(4);
		eaddr->sl(n);
		break;
	    } else
		throw Fault{ eINVAL, pc };
	};

	static auto ea_test = [&](int_t n) -> void {
	    if(eamode == DReg)
		utest<36>(n);
	    else switch(easz) {
	      case 0: utest<9>(n); break;
	      case 1: utest<18>(n); break;
	      default: utest<36>(n); break;
	    }
	};

	bool jump = false;

	/*  */ if(opcode == "000'0xx'xxx"_m) {			// Bcc
	    uint_t	dest = instr.addr;
	    if(opcode == "000'01x"_m) {
		instr.execs(2);
		dest = instr->uw() << 9;
		instr += 2;
		dest = instr.addr + sex_<27>(dest | (opcode & 0777));
	    } else
		dest = instr.addr + sex_<9>(opcode & 0777);
	    switch((opcode>>9) & 017) {
	      case 000: {					// BSR (no point to BRN)
		  jump = true;
		  Addr tos = addr(a[7]);
		  a[7].addr += 6;
		  tos.writes(6);
		  tos->uw(pc.seg);
		  tos->ul(instr.addr);
		  break;
	      }
	      case 001: jump = true; break;			// BRA
	      case 002: jump = (ccr&Z); break;			// BEQ
	      case 003: jump = !(ccr&Z); break;			// BNE
	      case 004: jump = (ccr&C); break;			// BLO / BCC
	      case 005: jump = !(ccr&C); break;			// BHS / BCS
	      case 006: jump = (ccr&Z) || (ccr&C); break;	// BLS
	      case 007: jump = !(ccr&Z) && !(ccr&C); break;	// BHI
	      case 010: jump = (ccr&N); break;			// BMI
	      case 011: jump = !(ccr&N); break;			// BPL
	      case 012: jump = (ccr&V); break;			// BLT / BVS
	      case 013: jump = !(ccr&V); break;			// BGE / BVC
	      case 014: jump = (ccr&V) || (ccr&Z); break;	// BLE
	      case 015: jump = !(ccr&V) && !(ccr&Z); break;	// BGT
	      default:
		throw Fault{ eINVAL, pc };
	    }
	    if(jump)
		pc.addr = dest;
	} else if(opcode == "000'100'001"_m) {			// RTS
	    a[7].addr -= 6;
	    Addr frame = addr(a[7]);
	    frame.reads(6);
	    pc = (SegAddr&)frame;
	    jump = true;
	} else if(opcode == "000'100'010"_m) {			// RTE
	    if(!(smr&SU))
		throw Fault{ ePERM, pc };
	    a[7].addr -= 24;
	    Addr fa = addr(a[7]);
	    fa.reads(24);
	    auto& frame = (ExceptionFrame&)fa;
	    ssp = a[7];
	    ccr = frame.ccr;
	    ir  = frame.ir;
	    smr = frame.smr;
	    a[7] = frame.usp;
	    pc = frame.pc;
	    jump = true;
	} else if(opcode == "000'100'011'"_m) {			// TRAP
	    pending |= 1l << ((opcode&017)+8);
	} else if(opcode == "1xx"_m) {				// OP Dn,EA / OP EA,Dn
	    bool todr = (opcode & (1<<8)) == 0;
	    int op = (opcode>>12) & 037;
	    int dreg = (opcode>>9) & 7;
	    int_t sarg;
	    uint_t uarg;
	    decltype(eamode) bea = eamode;
	    uword_t ber = ereg;


	    if(todr) {
		ea_read();
		sarg = sinput;
		uarg = uinput;
		uinput = unsigned_<36>(d[dreg].data);
		sinput = sex_<36>(d[dreg].data);
		eamode = DReg;
		ereg = dreg;
	    } else {
		uarg = unsigned_<36>(d[dreg].data);
		sarg = sex_<36>(d[dreg].data);
		// MOV and SEX do not need to read the destination
		if(op > 1)
		    ea_read();
	    }
	    switch(op) {
	      case 000: ea_uwrite(uarg); break;			// MOV
	      case 001: ea_swrite(sarg); break;			// SEX
	      case 010: ea_swrite(sinput+sarg); break;		// ADD
	      case 012: ea_uwrite(uinput+uarg+(ccr&C)); break;	// ADC
	      case 013: ea_uwrite(uinput-uarg-(ccr&C)); break;	// SBC
	      case 014: ea_uwrite(uinput&uarg); break;		// AND
	      case 015: ea_uwrite(uinput|uarg); break;		// OR
	      case 016: ea_uwrite(uinput^uarg); break;		// XOR
	      case 011: ea_swrite(sinput-sarg); /* fallthru */	// SUB
	      case 017: ccr&C = uarg>uinput;			// CMP
			ccr&V = sarg>sinput;
			ccr&Z = (uinput==sinput)? (uinput==uarg): (sinput==sarg);
			break;
	      case 020:	{					// MULU
		  uintmax_t prod = uinput*uarg;
		  ea_uwrite(prod);
		  ccr&C = ccr&V = carry_<36>(prod);
	        }
		break;
	      case 021:	{					// MULS
		  intmax_t prod = sinput*sarg;
		  ea_swrite(prod);
		  ccr&V = overflow_<36>(prod);
	        }
		break;
	      case 022:						// DIVU
		if(uarg) {
		    uint_t rem = uinput%uarg;
		    uint_t q = uinput/uarg;
		    if(todr) {
			eamode = bea;
			ereg = ber;
			ea_uwrite(rem);
			utest<36>(d[dreg].data = unsigned_<36>(q));
		    } else {
			d[dreg].data = unsigned_<36>(rem);
			ea_uwrite(rem);
		    }
		} else {
		    ccr&V = true;
		}
		break;
	      case 023:						// DIVS
		if(sarg) {
		    int_t rem = sinput%sarg;
		    int_t q = sinput/sarg;
		    if(todr) {
			eamode = bea;
			ereg = ber;
			ea_swrite(rem);
			stest<36>(d[dreg].data = signed_<36>(q));
		    } else {
			d[dreg].data = signed_<36>(rem);
			ea_swrite(rem);
		    }
		} else {
		    ea_swrite(1l<<36);
		}
		break;
	      case 024: ccr&C = uinput&1;			// ROR
			ea_uwrite((uinput>>1) | ((uinput&1)<<(ea_bits-1)));
			break;
	      case 025:						// RORC
			ea_uwrite((uinput>>1) | (ccr&C? 1l<<(ea_bits-1): 0));
			ccr&C = uinput&1;
			break;
	      case 026: ccr&C = uinput&(1l<<(ea_bits-1));	// ROL
			ea_uwrite((uinput<<1) | (ccr&C? 1: 0));
			break;
	      case 027:						// ROLC
			ea_uwrite((uinput<<1) | (ccr&C? 1: 0));
			ccr&C = uinput&(1l<<(ea_bits-1));
			break;
	      case 030: ea_uwrite(uinput^(1l<<uarg)); break;	// BCOM
	      case 031: ea_uwrite(uinput&~(1l<<uarg)); break;	// BCLR
	      case 032: ea_uwrite(uinput|(1l<<uarg)); break;	// BSET
	      case 033: ea_test(uinput&(1l<<uarg)); break;	// BTST
	      case 034: ea_swrite(sinput/(1ul<<uarg)); break;	// ASR
	      case 035: ea_uwrite(uinput>>uarg);		// LSR
			ccr&C = (uinput&1)!=0; break;
	      case 036: ea_swrite(sinput<<uarg); break;		// ASL
	      case 037: ea_uwrite(uinput<<uarg); break;		// LSL
	      default:
		throw Fault{ eINVAL, pc };
	    }
	} else if(opcode == "010'00x'xxx'0xx"_m) {	// OP EA
	    int op = (opcode>>9) & 017;

	    // CLR does not need to read the destination
	    if(op > 0)
		ea_read();
	    switch(op) {
	      case 000: ea_uwrite(0); break;			// CLR
	      case 004: ea_test(uinput); break;			// TST
	      case 005: ea_swrite(sinput+1); break;		// INC
	      case 006: ea_swrite(sinput-1); break;		// DEC
	      case 007: ea_swrite(-sinput); break;		// NEG
	      case 010: ea_uwrite(~sinput); break;		// COM
	      default:
		throw Fault{ eINVAL, pc };
	    }
	} else if(opcode == "010'010'xxx'000"_m) {		// STS An,EA
	    ea_readjust(2);
	    eaddr.writes(2);
	    eaddr->uw(a[(opcode>>9)&7].seg);
	} else if(opcode == "010'010'xxx'100"_m) {		// LDS EA,An
	    ea_readjust(2);
	    eaddr.reads(2);
	    a[(opcode>>9)&7].seg = eaddr->uw();
	} else if(opcode == "010'010'xxx'001"_m) {		// STA An,EA
	    if(eamode == DReg) {
		d[ereg].data = unsigned_<36>(a[(opcode>>9)&7].addr);
	    } else {
		ea_readjust(6);
		eaddr.writes(6);
		SegAddr& s = (SegAddr&)eaddr;
		s = a[(opcode>>9)&7];
		eaddr.writes(6);
	    }
	} else if(opcode == "010'010'xxx'101"_m) {		// LDA EA,An
	    if(eamode == DReg) {
		a[(opcode>>9)&7].addr = unsigned_<36>(d[ereg].data);
	    } else {
		ea_readjust(6);
		eaddr.reads(6);
		a[(opcode>>9)&7] = (SegAddr&)eaddr;
	    }
	} else if(opcode == "010'010'xxx'110"_m) {		// LEA EA,An
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    a[(opcode>>9)&7].seg = eaddr.seg->seg;
	    a[(opcode>>9)&7].addr = eaddr.addr;
	} else if(opcode == "011'000'000'x00"_m) {		// MOVM
	    uword_t	regs = instr->uw();
	    uword_t	size = 0;
	    instr += 2;
	    for(int i=0; i<18; i++)
		if(regs & (1<<i))
		    size += (i>14)? 2: ((i>7)? 6: 4);
	    ea_readjust(size);
	    if(regs&(3<<16) && !(smr&SU))
		throw Fault { ePERM, pc };
	    if(opcode & (1<<8)) {
		eaddr.reads(size);
		for(int i=0; i<18; i++) if(regs & (1<<i)) {
		    if(i<8) {
			d[i].data = eaddr->ul();
			eaddr += 2;
		    } else if(i<15) {
			a[i&7] = (SegAddr&)eaddr;
			eaddr += 4;
		    } else switch(i) {
		      case 15:	ccr = eaddr->uw(); break;
		      case 16:	ir  = eaddr->uw(); break;
		      case 17:	smr = eaddr->uw(); break;
		    }
		    eaddr += 2;
		}
	    } else {
		eaddr.writes(size);
		for(int i=0; i<18; i++) if(regs & (1<<i)) {
		    if(i<8) {
			eaddr->ul(d[i].data);
			eaddr += 2;
		    } else if(i<15) {
			(SegAddr&)eaddr = a[i&7];
			eaddr += 4;
		    } else switch(i) {
		      case 15:	eaddr->uw(ccr); break;
		      case 16:	eaddr->uw(ir); break;
		      case 17:	eaddr->uw(smr); break;
		    }
		    eaddr += 2;
		}
	    }
	} else if(opcode == "011'000'000'001"_m) {		// JSR
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    Addr tos = addr(a[7]);
	    tos.writes(6);
	    tos[0].uw(pc.seg);
	    tos[2].ul(instr.addr);
	    a[7].addr += 6;
	    jump = true;
	    pc.seg = eaddr.seg->seg;
	    pc.addr = eaddr.addr;
	} else if(opcode == "011'000'000'010"_m) {		// JMP
	    jump = true;
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    pc.seg = eaddr.seg->seg;
	    pc.addr = eaddr.addr;
	} else if(opcode == "011'000'001'010"_m) {		// PEA
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    Addr tos = addr(a[7]);
	    tos.writes(6);
	    tos[0].uw(eaddr.seg->seg);
	    tos[2].ul(eaddr.addr);
	    a[7].addr += 6;
	} else if(opcode == "011'000'001'0xx"_m) {		// Sxxx
	    if(!(smr&SU))
		throw Fault { ePERM, pc };
	    ea_read();
	    switch((opcode>>6) & 3) {
	      case 0: segmap = unsigned_<36>(uinput); break;	// SSMA
	      case 1: segmap_len = unsigned_<18>(uinput); break;// SSML
	      default:
	        throw Fault{ eINVAL, pc };
	    }
	} else switch(opcode) {
	  case 0133000:						// NOP
	    break;
	  case 0133001:						// STOP
	    halted = true;
	    break;
	  case 0133002:						// BKPT
	    pending |= 1l << int(eBREAK);
	    break;
	  case 0133003:						// CLC
	    ccr&C = false;
	    break;
	  default:
	    throw Fault{ eINVAL, pc };
	}

	if(!jump) {
	    pc.addr = instr.addr;
	};

#ifdef DEBUG

	if(halted && dodebug) {
	    display_cpu();
	    display_insn_decode();
	}

#endif

    } catch(const Fault& f) {
	if(f.trap == eLOOP) // Double fault.  Give up.
	    halted = true;
	else {
	    pending |= 1l << int(f.trap);
	    fault = f.fault;
	    if(instr) {
		pc.addr = instr.addr;
		pc.seg = instr.seg->seg;
	    }
	}
    }

}



bool CPU::apply(Object& obj, bool super)
{
    for(const auto& s: obj.segs) {
	uint_t	base = 0;
	if(super) {
	    if(s.value != 0777777)
		continue;
	    for(const auto& d: s.data)
		if(d.addr+d.bytes.size() <= mach.mem_alloc)
		    memcpy(mach.mem+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));
	}
    }
    if(obj.slines.size() || obj.syms.size())
	debug = &obj;
    return true;
}


#ifdef DEBUG

SCREEN*	CPU::debug_scr = nullptr;

#endif

} // namespace Nightmare




int main(int argc, char** argv)
{
    Nightmare::Machine	machine;
    Nightmare::CPU	cpu(machine);
    Nightmare::Object	bootstrap;

#ifdef DEBUG

    setlocale(LC_CTYPE, "");

    int	    pmaster;
    int	    pslave;
    char    ptyname[256]; // should be enough but seriously glibc?
    termios tio = {
	.c_iflag = IGNBRK|IUTF8,
	.c_oflag = 0,
	.c_cflag = 0,
	.c_lflag = 0,
    };
    if(openpty(&pmaster, &pslave, ptyname, &tio, nullptr)) {
	perror("openpty:");
	return 1;
    }

    if(true) { // send console to pty
	machine.stdin = machine.stdout = pmaster;
	cpu.debug_scr = newterm(nullptr, stdout, stdin);
    } else {
	machine.stdin = 0;
	machine.stdout = 1;
	FILE* slave = fdopen(pslave, "r+");
	cpu.debug_scr = newterm(nullptr, slave, slave);
    }

    std::string cmd = std::format("screen -X screen -t console '{}';screen -X other", ptyname);
    system(cmd.c_str());

    noecho();
    cbreak();

#else // DEBUG

    termios tio, ptio;

    if(!tcgetattr(0, &ptio)) {
	tio = ptio;
	tio.c_iflag = IUTF8;
	tio.c_oflag = 0;
	tio.c_cflag = 0;
	tio.c_lflag = ISIG;
	tcsetattr(0, TCSANOW, &tio);
    }

#endif

    std::ifstream	bsfile("bootstrap.x");
    if(bsfile.bad()) {
	std::cerr << std::format("{}: bootstrap.x: {}", argv[0], std::strerror(errno)) << std::endl;
	return 1;
    }
    bootstrap.load(bsfile);

    machine.mem = new Nightmare::byte_t[machine.mem_alloc = 640*1024]; // 640K ought to be enough for anyone.  :-)
    memset(machine.mem, 0, machine.mem_alloc*sizeof(Nightmare::byte_t));
    cpu.apply(bootstrap, true);

    if(!cpu.reset())
	cpu.run();

#ifdef DEBUG

    endwin();

#else

    tcsetattr(0, TCSANOW, &ptio);

#endif

    std::cerr << std::format("\n\n--- Run complete, {} instructions decoded", cpu.insn) << std::endl;

}

