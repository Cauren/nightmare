#include <stdint.h>
#include <cstddef>
#include <concepts>

namespace Nightmare {

    struct Byte {
	uint16_t		n: 9;
    };

    struct DReg {
	uint64_t		n: 36;

	static const uint64_t	sbit = uint64_t(1)<<35;
	static const uint64_t	smask = ~(sbit-1);
	static const uint64_t	umask = smask^sbit;
	static const uint64_t	vmask = ~smask;
	static const uint64_t	nan = sbit;

	template<std::unsigned_integral T>
	DReg&			operator = (T v) {
				    n = v;
				    return *this;
				};
	template<std::signed_integral T>
	DReg&			operator = (T v) {
				    n = v<0? sbit|-v: v;
				    return *this;
				};
	DReg&			operator = (uint64_t v) {
				    n = v&umask? nan: v;
				    return *this;
				};
	DReg&			operator = (int64_t v) {
				    if(v<0) {
					v = -v;
					n = v&smask? nan: v|sbit;
				    } else
					n = v&smask? nan: v;
				    return *this;
				};
	explicit		operator uint64_t () const {
				    return n;
				};
				operator int64_t () const {
				    return n&sbit? ((-n)&~smask): n;
				};

	bool			isnan(void) const					{ return n == sbit; };
    };

    struct AReg {
	uint32_t		seg: 18;
	uint64_t		addr: 36;

				AReg()							{ };
				AReg(const Areg& a): seg(a.seg), addr(a.addr)		{ };
				AReg(uint32_t s, uint64_t a): seg(s), addr(a)		{ };

	AReg			operator + (uint64_t n) const				{ return AReg(seg, addr+n); };
    };

    struct Machine {
	size_t			mem_alloc;
	Byte*			mem;
    };

    struct Segment {
	uint32_t		seg: 18;
	bool			valid: 1,
				super: 1,
				read: 1,
				write: 1,
				exec: 1;
	uint64_t		addr:36;
	uint64_t		len:36;
    };

    struct Trap {
	AReg			ea;
	uint16_t		num;

				Trap(uint16_t n, const AReg& areg): ea(areg), num(n)	{ };
    };

    struct Halt {
	uint16_t		breakpoint;

				Halt(uint16_t bp=0): breakpoint(bp)			{ };
    };

    struct CPU {
	Machine&		m;

	AReg			pc;
	DReg			d[8];
	AReg			a[8];
	AReg			ssp;
	union {
	    uint16_t		ccr: 9;
	    struct {
		bool			z: 1,
					n: 1,
					v: 1;
	    }			cc;
	};
	uint16_t		ir: 9;
	union {
	    uint16_t		smr: 9;
	    struct {
		bool			signal: 1,
					super: 1,
					reset: 1,
					restart: 1;
	     }			sm;
	};

	Segment			segs[16];
	uint16_t		pending;
	AReg			fault;
	bool			halted;

				CPU(Machine& mach): m(mach)			{ reset(); };

	void			display(void) const;

	void			run(void);

	const Segment&		seg(const AReg&);
	Byte*			mem(const AReg&, size_t len, bool write=false);
	static uint64_t		read1(const Byte* b)				{ return b->n; };
	static uint64_t		read2(const Byte* b)				{ return read1(b)<<9 | read1(b+1); };
	static uint64_t		read4(const Byte* b)				{ return read2(b) | read2(b+2)<<18; };
	static int64_t		sex1(uint64_t n)				{ return (n&(1<<8))?  (1<<8)-n:  n; };
	static int64_t		sex2(uint64_t n)				{ return (n&(1<<17))? (1<<17)-n: n; };
	static int64_t		sex3(uint64_t n)				{ return (n&(1<<26))? (1<<26)-n: n; };
	static int64_t		sex4(uint64_t n)				{ return (n&(1<<35))? (1<<35)-n: n; };
	static void		write1(Byte* b, uint16_t n)			{ b->n = n; };
	static void		write2(Byte* b, uint32_t n)			{ write1(b, n>>9); write1(b+1, n); };
	static void		write4(Byte* b, uint64_t n)			{ write2(b, n); write2(b+2, n>>18); };

	void			stores(const AReg& ea, size_t len, int64_t s) {
				    Byte* d = mem(ea, len, true);
				    int64_t sign = 1 << ((9*len)-1);
				    if(s<0) {
					s = -s;
					if(s >= sign) {
					    s = 0;
					    cc.v = true;
					}
					s |= sign;
				    } else if(s >= sign) {
					s = sign;
					cc.v = true;
				    }
				    storeu(ea, len, s);
				};
	void			reset(void);
	void			trap(uint16_t num, const AReg&);
    };

};

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

segment 0: flat memory / ring 0
segment 1: traps (sans reset)
segment 2: segment map

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
| 1   0 |      op       |     dr    | d |  eam  |  ea.type  |   ea.reg  |	OP ea,dr  dr,ea
| 1   1   0   0   0 |      op       | 0 |  eam  |  ea.type  |   ea.reg  |	OP ea
| 1   1   0   0   0   0 |     ar    | 1   0   0 |  ea.type  |   ea.reg  |	LDS ea,ar
| 1   1   0   0   0   0 |     ar    | 1   0   1 |  ea.type  |   ea.reg  |	LDA ea,ar
| 1   1   0   0   0   0 |     ar    | 1   1   1 |  ea.type  |   ea.reg  |	LEA ea,ar
| 1   1   0   0   0   1 |     ar    | 1   0   0 |  ea.type  |   ea.reg  |	STS ar,ea
| 1   1   0   0   0   1 |     ar    | 1   0   1 |  ea.type  |   ea.reg  |	STA ar,ea
| 1   1   1   0   0   0   0   0   0   0   0   0 |  ea.type  |   ea.reg  |	JSR ea
| 1   1   1   0   0   0   0   0   0   0   0   1 |  ea.type  |   ea.reg  |	JMP ea

| 0   0   0   1   0   0   0   0   0 |	bsr	r9
| 0   0   0   1   0   0   0   0   1 |	rts	#unwind9
| 0   0   0   1   0   0   0   1   0 |	rte	#unwind9
| 0   0   0   1   0   0   0   1   1 |	trap	#n

| 0   0   0   1   1   0   0   0   0 |	bsr	r27


ea type:
000	dr
001	(ar)
010	(ar)+
011	-(ar)
100	(d18,ar)
101	sr:d36
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
