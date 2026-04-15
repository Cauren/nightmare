#define _XOPEN_SOURCE_EXTENDED

#include <ncursesw/curses.h>
#include <string>
#include <format>
#include <cerrno>
#include <cstring>
#include <fstream>
#include "cpu.hh"
#include "object.hh"

namespace Nightmare {

template<typename BIT>
Nightmare::CPU::Bitreg<BIT>::Bits operator | (BIT b1, BIT b2)
{
    return typename Nightmare::CPU::Bitreg<BIT>::Bits(b1) | b2;
};

CPU::Addr CPU::addr(uword_t segno, uint_t a, bool super)
{
    Segment& s = scache[segno&15];
    super |= smr & SU;

    if(!s.valid || s.seg!=segno) {
	switch(segno) {
	  case 0777777:
	    s = { segno, true, true, true, true, true, mem_alloc_, mem_ };
	    break;
	  case 0777776:
	    s = { segno, true, true, true, true, false, 16*segmap_len, mem_+segmap };
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

	throw Fault{ eLOOP, faddr };

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
	    None,
	    Immed, DReg,
	    Absolute,
	    PostInc, PreDec, Indirect, PreIndex, PostIndex
	}	eamode;
	static const char* const    eamode_name[] = {
	    "None", "Immediate", "DReg",
	    "Absolute", "PostInc", "PreDec", "Indirect", "PreIndex", "PostIndex"
	};

	eamode = None;
	if((opcode>>16)&3) {
	    // bit 17 or 16 set means there are EA fields on the opcode

	    eamode = Indirect;
	    if(opcode == "1"_m || opcode == "010'00x'xxx'0"_m) // those have eam field
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
			uword_t sn = instr.uword();
			eaddr = addr(sn, instr.ulong());
			break;
		    } else if((opcode&077) != 070) {
			throw Fault{ eINVAL, pc };
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

	if(dodebug && debug) {
	    mvaddstr(0, 0, "┏━━━┯━━━━━━━━━━━━━┳━━━┯━━━━━━━━━━━━━━━━━━━━┓");
	    for(int i=0; i<8; i++) {
		std::string ln = std::format("┃ d{}│{:12o} ┃ a{}│{:>6o}:{:012o} ┃",
					     i, signed_<36>(d[i].data),
					     i, unsigned_<18>(a[i].seg), unsigned_<36>(a[i].addr));
		mvaddstr(i+1, 0, ln.c_str());
	    }
	    mvaddstr(9, 0, "┗━━━┷━━━━━━━━━━━━━┻━━━┷━━━━━━━━━━━━━━━━━━━━┛");

	    mvaddstr(0, 46, "Decoded EA: "); addstr(eamode_name[int(eamode)]); clrtoeol();

	    auto ppc = debug->slines.upper_bound(Object::SourceLine{ pc.addr, pc.seg });
	    int dln = 1;
	    while(dln>-2 && ppc!=debug->slines.begin()) {
		if(std::prev(ppc)->seg != pc.seg)
		    break;
		ppc--;
		dln--;
	    }
	    mvaddstr(11, 0, std::format("PC: {:06o}:{:012o}", pc.seg, pc.addr).c_str());
	    for(int ln=-3; ln<10; ln++) {
		move(ln+14, 29);
		if(ln<dln || ppc->seg!=pc.seg) {
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

	    refresh();
	    char c = getch();
	    if(c == 'b')
		__asm__("int $3");
	    if(c == 'c')
		dodebug = false;
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
		    uinput = rea.ubyte();
		    sinput = sex_<9>(uinput);
		    break;
		  case 1:
		    rea.reads(2);
		    uinput = rea.uword();
		    sinput = sex_<18>(uinput);
		    break;
		  default:
		    rea.reads(4);
		    uinput = rea.ulong();
		    sinput = sex_<36>(uinput);
		    break;
		}
	    } else
		sinput = sex_(ea_bits, uinput);
	};

	static auto ea_uwrite = [&](int_t n) -> void {
	    if(eamode == DReg) {
		utest<36>(n);
		d[ereg].data = unsigned_<36>(n);
	    } else if(memea) switch(easz) {
	      case 0:
		utest<9>(n);
		eaddr.writes(1);
		eaddr.ubyte(n);
		break;
	      case 1:
		utest<18>(n);
		eaddr.writes(2);
		eaddr.uword(n);
		break;
	      default:
		utest<36>(n);
		eaddr.writes(4);
		eaddr.ulong(n);
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
		eaddr.sbyte(n);
		break;
	      case 1:
		stest<18>(n);
		eaddr.writes(2);
		eaddr.sword(n);
		break;
	      default:
		stest<36>(n);
		eaddr.writes(4);
		eaddr.slong(n);
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
		instr.reads(2);
		dest = instr.uword() << 9;
		dest = instr.addr + sex_<27>(dest | (opcode & 0777));
	    } else
		dest = instr.addr + sex_<9>(opcode & 0777);
	    switch((opcode>>9) & 017) {
	      case 000: {					// BSR (no point to BRN)
		  jump = true;
		  Addr tos = addr(a[7]);
		  tos.writes(6);
		  tos.uword(pc.seg);
		  tos.ulong(instr.addr);
		  a[7].addr += 6;
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
	    pc.seg = frame.uword();
	    pc.addr = frame.ulong();
	    jump = true;
	} else if(opcode == "000'100'010"_m) {			// RTE
	    if(!(smr&SU))
		throw Fault{ ePERM, pc };
	    a[7].addr -= 24;
	    Addr frame = addr(a[7]);
	    ssp = a[7];
	    frame.reads(24);
	    ccr = frame.uword();
	    ir  = frame.uword();
	    smr = frame.uword();
	    frame.areg();
	    a[7] = frame.areg();
	    pc = frame.areg();
	    jump = true;
	} else if(opcode == "000'100'011'"_m) {			// TRAP
	    pending |= 1l << ((opcode&017)+8);
	} else if(opcode == "1xx"_m) {				// OP Dn,EA / OP EA,Dn
	    bool todr = (opcode & (1<<8)) == 0;
	    int op = (opcode>>12) & 037;
	    int dreg = (opcode>>9) & 7;
	    int_t sarg;
	    uint_t uarg;

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
	      case 011: ea_swrite(sinput-sarg); break;		// SUB
	      case 012: ea_uwrite(uinput+uarg+(ccr&C)); break;	// ADC
	      case 013: ea_uwrite(uinput-uarg-(ccr&C)); break;	// SBC
	      case 014: ea_uwrite(uinput&uarg); break;		// AND
	      case 015: ea_uwrite(uinput|uarg); break;		// OR
	      case 016: ea_uwrite(uinput^uarg); break;		// XOR
	      case 017: ccr&C = uarg>uinput;			// CMP
			ccr&V = sarg>sinput;
			ccr&Z = (uinput==sinput)? (uinput==uarg): (sinput==sarg);
			break;
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
	    eaddr.uword(a[(opcode>>9)&7].seg);
	} else if(opcode == "010'010'xxx'100"_m) {		// LDS EA,An
	    ea_readjust(2);
	    eaddr.reads(2);
	    a[(opcode>>9)&7].seg = eaddr.uword();
	} else if(opcode == "010'010'xxx'001"_m) {		// STA An,EA
	    if(eamode == DReg) {
		d[ereg].data = unsigned_<36>(a[(opcode>>9)&7].addr);
	    } else {
		ea_readjust(6);
		eaddr.writes(6);
		eaddr.uword(a[(opcode>>9)&7].seg);
		eaddr.ulong(a[(opcode>>9)&7].addr);
	    }
	} else if(opcode == "010'010'xxx'101"_m) {		// LDA EA,An
	    if(eamode == DReg) {
		a[(opcode>>9)&7].addr = unsigned_<36>(d[ereg].data);
	    } else {
		ea_readjust(6);
		eaddr.reads(6);
		a[(opcode>>9)&7].seg = eaddr.uword();
		a[(opcode>>9)&7].addr = eaddr.ulong();
	    }
	} else if(opcode == "010'010'xxx'110"_m) {		// LEA EA,An
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    a[(opcode>>9)&7].seg = eaddr.seg->seg;
	    a[(opcode>>9)&7].addr = eaddr.addr;
	} else if(opcode == "011'000'000'x00"_m) {		// MOVM
	    uword_t	regs = instr.uword();
	    uword_t	size = 0;
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
			d[i].data = eaddr.ulong();
		    } else if(i<15) {
			a[i&7].seg = eaddr.uword();
			a[i&7].addr = eaddr.ulong();
		    } else switch(i) {
		      case 15:	ccr = eaddr.uword(); break;
		      case 16:	ir  = eaddr.uword(); break;
		      case 17:	smr = eaddr.uword(); break;
		    }
		}
	    } else {
		eaddr.writes(size);
		for(int i=0; i<18; i++) if(regs & (1<<i)) {
		    if(i<8) {
			eaddr.ulong(d[i].data);
		    } else if(i<15) {
			eaddr.uword(a[i&7].seg);
			eaddr.ulong(a[i&7].addr);
		    } else switch(i) {
		      case 15:	eaddr.uword(uword_t(ccr)); break;
		      case 16:	eaddr.uword(uword_t(ir)); break;
		      case 17:	eaddr.uword(uword_t(smr)); break;
		    }
		}
	    }
	} else if(opcode == "011'000'000'001"_m) {		// JSR
	    if(!eaddr)
		throw Fault{ eFAULT, pc };
	    Addr tos = addr(a[7]);
	    tos.writes(6);
	    tos.uword(pc.seg);
	    tos.ulong(instr.addr);
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
	} else
	    throw Fault{ eINVAL, pc };

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



bool CPU::apply(const Object& obj, bool super)
{
    for(const auto& s: obj.segs) {
	uint_t	base = 0;
	if(super) {
	    if(s.value != 0777777)
		continue;
	    for(const auto& d: s.data)
		if(d.addr+d.bytes.size() <= mem_alloc_)
		    memcpy(mem_+d.addr, d.bytes.data(), d.bytes.size()*sizeof(byte_t));
	}
    }
    if(obj.slines.size() || obj.syms.size())
	debug = &obj;
    return true;
}



} // namespace Nightmare




int main(int argc, char** argv) {
    setlocale(LC_CTYPE, "");
    initscr();
    noecho();
    cbreak();

    Nightmare::CPU	cpu;
    Nightmare::Object	bootstrap;

    std::ifstream	bsfile("bootstrap.x");
    if(bsfile.bad()) {
	std::cerr << std::format("{}: bootstrap.x: {}", argv[0], std::strerror(errno)) << std::endl;
	return 1;
    }
    bootstrap.load(bsfile);

    cpu.mem_ = new Nightmare::byte_t[cpu.mem_alloc_ = 640*1024]; // 640K ought to be enough for anyone.  :-)
    cpu.apply(bootstrap, true);

    if(!cpu.reset())
	cpu.run();

    endwin();
}

