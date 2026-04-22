#include <cstdint>
#include <cstddef>
#include <concepts>

#include "machine.hh"

#ifndef NIGHTMARE_CPU_HH__
#define NIGHTMARE_CPU_HH__

namespace Nightmare {

    class Object;

    class CPU {
	public:
	    Machine&		mach;

	public:

	    enum Trap_t {
		Reset,
		eLOOP, eFAULT, eINVAL, ePERM, eACCES,
		eBREAK,
	    };

	    struct Segment: public InMem {
		static constexpr int	EXEC =	0000001;
		static constexpr int	WRITE =	0000002;
		static constexpr int	READ =	0000004;
		static constexpr int	SUPER =	0000010;
		static constexpr int	COW =	0000020;
		static constexpr int	VALID =	0400000;

		ULong			base;
		ULong			size;
		UWord			flags;
		UWord			spare_;
		ULong			segid;
	    };

	    struct ExceptionFrame: public InMem {
		UWord			ccr;
		UWord			ir;
		UWord			smr;
		SegAddr			fault;
		SegAddr			usp;
		SegAddr			pc;
	    };

	    struct CSeg {				// cached segment info
		uword_t			seg;
		uword_t			flags;
		uint_t			len;
		MemPtr			mem;
	    };
	    static CSeg			sixseven;

	    // no struct AReg {}
	    // it's defined in machine.hh

	    struct Fault {
		Trap_t			trap;
		AReg			fault;
	    };

	    struct Addr {
		CSeg*			seg;
		uint_t			addr;

					Addr(void): seg(nullptr)					{ };
					Addr(Addr&&) = default;
					Addr(const Addr&) = default;
					Addr(CSeg* s, uint_t a): seg(s), addr(a)			{ };

					operator bool (void) const		{ return seg; };
					operator AReg (void) const		{ return AReg{ addr, seg? seg->seg: 0777777 }; };

		inline void		reads(uint_t len)			{ access(len, Segment::READ); };
		inline void		writes(uint_t len)			{ access(len, Segment::WRITE); };
		inline void		execs(uint_t len)			{ access(len, Segment::EXEC); };
		void			access(uint_t len, int perm);

		Addr&			operator = (nullptr_t)			{ seg = nullptr; return *this; };
		Addr&			operator = (Addr&&) = default;
		Addr&			operator = (const Addr&) = default;
		Addr&			operator += (int_t len)			{ addr += len; return *this; };
		Addr&			operator -= (int_t len)			{ addr -= len; return *this; };

		Bytes*			operator -> (void) const		{ return &seg->mem[addr]; };
		Bytes&			operator [] (int_t i) const		{ return seg->mem[addr+i]; };
		template<std::derived_from<InMem> T>
		    T&			ref(int_t i=0) const			{ return seg->mem.ref<T>(addr, i); };
		template<std::derived_from<InMem> T>
					operator T& (void) const		{ return seg->mem.ref<T>(addr); };
	    };

	    struct DReg {
		int_t			data;
	    };

	    template<typename BIT> struct Bitreg {
		uword_t			bits;

		struct Bref {
		    Bitreg&		reg;
		    uword_t		bit;

					operator bool (void) const		{ return reg.bits & bit; };
		    bool		operator ++ (int)			{ reg.bits |= bit; return true; };
		    bool		operator -- (int)			{ reg.bits &= ~bit; return false; };
		    bool		operator = (bool b)			{ if(b) reg.bits |= bit;
										  else  reg.bits &= ~bit;
										  return b; };
		};
		struct Bits {
		    uword_t		bits;

					Bits(void): bits(0)			{ };
					Bits(BIT b)				{ bits = 1<<int(b); };
					Bits(uword_t bi)			{ bits = bi; };
					Bits(Bits&&) = default;
					Bits(const Bits&) = default;

		    Bits		operator | (const Bits& bi) const	{ return bits|bi.bits; };
		};

					Bitreg(void): bits(0)			{ };
					Bitreg(uword_t b)			{ bits = b; };
					Bitreg(Bitreg&&) = default;
					Bitreg(const Bitreg&) = default;

		Bitreg&			operator = (BIT b)			{ bits = 1<<int(b); return *this; };
		Bitreg&			operator = (Bits b)			{ bits = b.bits; return *this; };
		Bitreg&			operator += (BIT b)			{ bits |= 1<<int(b); return *this; };
		Bitreg&			operator += (Bits b)			{ bits |= b.bits; return *this; };
		Bitreg&			operator -= (BIT b)			{ bits &= ~(1<<int(b)); return *this; };
		Bitreg&			operator -= (Bits b)			{ bits &= ~b.bits; return *this; };
		Bitreg&			operator = (Bitreg&&) = default;
		Bitreg&			operator = (const Bitreg&) = default;
		Bitreg&			operator = (uword_t b)			{ bits = b; return *this; };

					operator uword_t (void) const		{ return bits; };
		Bref			operator & (BIT b)			{ return { *this, uword_t(1)<<int(b) }; };
		const Bref		operator & (BIT b) const		{ return { *this, uword_t(1)<<int(b) }; };
	    };

	    AReg			pc;
	    DReg			d[8];
	    AReg			a[8];
	    AReg			ssp;
	    enum CCBits			{ Z, C, V, N, };
	    enum SMBits			{ SU, RST, FAULT };
	    Bitreg<CCBits>		ccr;
	    Bitreg<SMBits>		smr;
	    uword_t			ir;

	    uint_t			segmap = 0;
	    uword_t			segmap_len = 0;

	    CSeg			scache[16];
	    uint64_t			pending = 0;
	    AReg			fault;

	    Object*			debug = nullptr;
	    bool			dodebug = true;

	    uint64_t			insn = 0;

	    template<uint_t bits> void utest(int_t v) {
		int_t sign = 1l << (bits-1);
		ccr&Z = v==0;
		ccr&V = v==sign;
		ccr&N = (v & sign) != 0;
		ccr&C = (v & ~((1l<<bits)-1));
	    };

	    template<uint_t bits> void stest(int_t v) {
		int_t sign = 1l << (bits-1);
		ccr&Z = v==0;
		ccr&N = v<0;
		ccr&V = v<=-sign || v>=sign;
		ccr&C = (v & ~((1l<<bits)-1));
	    };

					CPU(Machine& m): mach(m)		{ };

	    CSeg*			seg(uword_t sn);
	    Addr			addr(uword_t sn, uint_t a, bool super=false);
	    Addr			addr(const AReg& ar)			{ return addr(ar.seg, ar.addr); };

	    bool			apply(Object&, bool super=false);
	    bool			reset(void);
	    void			invalidate(void);
	    void			trap(byte_t num, const AReg& t);
	    void			run(void);
	    void			oscall(void);
    };

    inline void CPU::Addr::access(uint_t len, int perm)
    {
	if(!seg || addr+len > seg->len)
	    throw Fault{ eFAULT, *this };
	if(!(seg->flags & perm))
	    throw Fault{ eACCES, *this };
    };

};

template<typename BIT>
Nightmare::CPU::Bitreg<BIT>::Bits operator | (BIT b1, BIT b2);


#endif // Double inclusion guard

#if 0


trap frame:
return seg
return addr
a7 seg
a7 addr
fault seg
fault addr
sm
ir
ccr
0

registers

d0-d7: 36 bit data registers
a0-a7: 54 bit address registers (18 segment, 36 offset)
	(a7 ~ sp treated magically like m68k)
pc: 54 bit address register
s0: 9 bit flags
s1: 9 bit interupt mask
s2: 9 bit mode
18 bit insn

segment 0400017: flat memory / ring 0
segment 0400016: fake seg for segmap

/// trap vectors
0: reset
1: double fault
2: bus error
3: illegal instruction
4: signaling nan
5: nmi

eam
---
0	byte ea
1	word ea
2	long ea
3	6 bit #imm

  17  16  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
| 1 |         op        |     dr    | d |  eam  |  ea.type  |   ea.reg  |	OP ea,dr  dr,ea

| 0   1   0   0   0 |      op       | 0 |  eam  |  ea.type  |   ea.reg  |	OP ea
| 0   1   0   0   1   0 |     ar    | 0   0   0 |  ea.type  |   ea.reg  |	STS ar,ea
| 0   1   0   0   1   0 |     ar    | 1   0   0 |  ea.type  |   ea.reg  |	LDS ea,ar
| 0   1   0   0   1   0 |     ar    | 0   0   1 |  ea.type  |   ea.reg  |	STA ar,ea
| 0   1   0   0   1   0 |     ar    | 1   0   1 |  ea.type  |   ea.reg  |	LDA ea,ar
| 0   1   0   0   1   0 |     ar    | 1   1   0 |  ea.type  |   ea.reg  |	LEA ea,ar

| 0   1   1   0   0   0   0   0   0 | d | 0   0 |  ea.type  |   ea.reg  |	MOVM regs...,ea  ea,regs...
| 0   1   1   0   0   0   0   0   0   0   0   1 |  ea.type  |   ea.reg  |	JSR ea
| 0   1   1   0   0   0   0   0   0   0   1   0 |  ea.type  |   ea.reg  |	JMP ea
| 0   1   1   0   0   0   0   0   1   0   0   0 |  ea.type  |   ea.reg  |	SSMA ea
| 0   1   1   0   0   0   0   0   1   0   0   1 |  ea.type  |   ea.reg  |	SSML ea
| 0   1   1   0   0   0   0   0   1   0   1   0 |  ea.type  |   ea.reg  |	PEA ea

| 0   0   0   0   0 |      xx       |                 r9                |	bxx r9
| 0   0   0   0   1 |      xx       |                 r9		|	bxx r27
| 0   0   0   1   0   0   0   0   1 |                 i9		|	rts #unwind9
| 0   0   0   1   0   0   0   1   0 |                 i9		|	rte #unwind9
| 0   0   0   1   0   0   0   1   1 |                 i9		|	trap #n

| 0   0   1   1   1   1   1   1   1   0   0   0   0   0   0   0   0   0 |	nop
| 0   0   1   1   1   1   1   1   1   0   0   0   0   0   0   0   0   1 |	stop
| 0   0   1   1   1   1   1   1   1   0   0   0   0   0   0   0   1   0 |	bkpt

TODO:	LINK/UNLK?
	MULS/MULU
	DIVS/DIVU
	ROL/ROR
	ROXL/ROXR
	BCHG


ea type:
000	dr
001	(ar)
010	(ar)+
011	-(ar)
100	(d18,ar)
101	sr:d18
110	extended
111:000	pc extended
111:001	immediate
111:010 absolute (relocatable)

ext ea

  17  16  15  14  13  12  11  10  9   8   7   6   5   4   3   2   1   0
| 0   0   0   0   0   0   0   0   0 |             d9                    |       (d9,ar)
| 0   0   1   0   0   0   0   0   0 |             d9                    |       ([d9,ar])
| 0   1   0 | n | 0   0   0   0   0 |             d9                    |+	(dn,ar,d9)
| 0   1   1 | n | 0   0   0   0   0 |             d9                    |+	([dn,ar],d9)
| 1   0   0 | n | scale |    dr     |             d9                    |+	(dn,ar,dr*scale,d9)
| 1   0   1 | n | scale |    dr     |             d9                    |+	([dn,ar],dr*scale,d9)
| 1   1   1 | n | scale |    dr     |             d9                    |+	([dn,ar,dr*scale],d9)

#endif
