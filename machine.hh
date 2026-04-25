#include <cstdint>
#include <cstddef>
#include <concepts>
#include <vector>

#ifndef NIGHTMARE_MACHINE_HH__
#define NIGHTMARE_MACHINE_HH__

namespace Nightmare {

    // smallest integral types that can hold a 36-bit long
    typedef uint64_t	uint_t;
    typedef int64_t	int_t;

    // smallest unsigned integral type that can hold an 18-bit word
    typedef uint32_t	uword_t;

    // smallest usigned integral type that can hold a 9-bit byte
    typedef uint16_t	byte_t;

    template<uint_t bits> constexpr bool overflow_(int_t n) {
	int_t sign = 1l << (bits-1);
	return n<=-sign || n>=sign;
    }

    template<uint_t bits> constexpr bool carry_(int_t n) {
	return n & ~((1l<<bits)-1);
    }

    template<uint_t bits> constexpr uint_t signed_(int_t n) {
	uint_t sign = 1l << (bits-1);

	return (n<0)?
	    ((n<=sign)? sign: sign|-n):
	    ((n>=sign)? sign: n);
    }
    inline static constexpr uint_t signed_(uint_t bits, int_t n) {
	uint_t sign = 1l << (bits-1);

	return (n<0)?
	    ((n<=sign)? sign: sign|-n):
	    ((n>=sign)? sign: n);
    }

    template<uint_t bits> constexpr uint_t unsigned_(int_t n) {
	return uint_t(n) & ((1l << bits) - 1);
    }
    inline static constexpr uint_t unsigned_(uint_t bits, int_t n) {
	return uint_t(n) & ((1l << bits) - 1);
    }

    template<uint_t bits> constexpr int_t sex_(uint_t n) {
	uint_t sign = 1l << (bits-1);
	return (n&sign)? -(n^sign): n;
    }
    inline static constexpr int_t sex_(uint_t bits, uint_t n) {
	uint_t	sign = 1l << (bits-1);
	return (n&sign)? -(n^sign): n;
    }

    class Bytes {

	private:
	    static byte_t	ub(const byte_t* b)		{ return b[0]; };
	    static uword_t	uw(const byte_t* b)		{ return uword_t(b[0])<<9 | b[1]; };
	    static uint_t	ul(const byte_t* b)		{ return uw(b) | uint_t(uw(b+2))<<18; };
	    static void		ub(byte_t* b, uint_t n)		{ b[0] = unsigned_<9>(n); };
	    static void		uw(byte_t* b, uint_t n)		{ ub(b, n>>9); ub(b+1, n); };
	    static void		ul(byte_t* b, uint_t n)		{ uw(b, n); uw(b+2, n>>18); };
	    static void		sb(byte_t* b, int_t n)		{ ub(b, signed_<9>(n)); };
	    static void		sw(byte_t* b, int_t n)		{ uw(b, signed_<18>(n)); };
	    static void		sl(byte_t* b, int_t n)		{ ul(b, signed_<36>(n)); };

	protected:
	  byte_t*		bytes(void)			{ return reinterpret_cast<byte_t*>(this); };
	  const byte_t*		bytes(void) const		{ return reinterpret_cast<const byte_t*>(this); };

	public:
	    byte_t		ub(void) const			{ return ub(bytes()); };
	    int_t		sb(void) const			{ return sex_<9>(ub()); };
	    uword_t		uw(void) const			{ return uw(bytes()); };
	    int_t		sw(void) const			{ return sex_<18>(uw()); };
	    uint_t		ul(void) const			{ return ul(bytes()); };
	    int_t		sl(void) const			{ return sex_<36>(ul()); };

	    void		ub(uint_t n)			{ ub(bytes(), n); };
	    void		sb(int_t n)			{ sb(bytes(), n); };
	    void		uw(uint_t n)			{ uw(bytes(), n); };
	    void		sw(int_t n)			{ sw(bytes(), n); };
	    void		ul(uint_t n)			{ ul(bytes(), n); };
	    void		sl(int_t n)			{ sl(bytes(), n); };
    };

    template<size_t N> class nBytes: protected Bytes {
	private:
	    byte_t		bytes_[N];

	public:
	    nBytes&		operator = (const nBytes&) = default;
	    nBytes&		operator = (nBytes&&) = default;
    };

    class UByte: public nBytes<1> {
	public:
	    UByte&		operator = (uint_t n)		{ ub(n); return *this; };
				operator uint_t () const	{ return ub(); };
    };

    class SByte: public nBytes<1> {
	public:
	    SByte&		operator = (int_t n)		{ sb(n); return *this; };
				operator int_t ()    const	{ return sb(); };
    };

    class UWord: public nBytes<2> {
	public:
	    UWord&		operator = (uint_t n)		{ uw(n); return *this; };
				operator uint_t () const	{ return uw(); };
    };

    class SWord: public nBytes<2> {
	public:
	    SWord&		operator = (int_t n)		{ sw(n); return *this; };
				operator int_t ()    const	{ return sw(); };
    };

    class ULong: public nBytes<4> {
	public:
	    ULong&		operator = (uint_t n)		{ ul(n); return *this; };
				operator uint_t () const	{ return ul(); };
    };

    class SLong: public nBytes<4> {
	public:
	    SLong&		operator = (int_t n)		{ sl(n); return *this; };
				operator int_t ()    const	{ return sl(); };
    };

    struct SegAddr;
    // This is defined here rather than within CPU because passing addresses
    // around in native format turns out to be necessary in multiple places.
    struct AReg {
	public:
	    uint_t		addr;
	    uword_t		seg;

	public:
	    AReg&		operator = (const SegAddr&);
    };

    class SegAddr {
	public:
	    UWord		seg;
	    ULong		addr;

	public:
	    SegAddr&		operator = (const AReg& ar)	{ seg = ar.seg; addr = ar.addr; return *this; };
    };

    inline AReg& AReg::operator = (const SegAddr& sa) {
	seg = sa.seg;
	addr = sa.addr;
	return *this;
    };

    class MemPtr {
	private:
	    byte_t*		ptr;

	public:
				MemPtr(byte_t* p = nullptr): ptr(p)				{ };
				MemPtr(const MemPtr&) = default;
				MemPtr(MemPtr&&) = default;

	    MemPtr&		operator = (nullptr_t)			{ ptr = nullptr; return *this; };
	    MemPtr&		operator = (byte_t* p)			{ ptr = p; return *this; };
	    MemPtr&		operator = (const MemPtr&)		= default;
	    MemPtr&		operator = (MemPtr&&)			= default;

	    Bytes&		operator [] (size_t i) const		{ return *reinterpret_cast<Bytes*>(ptr+i); };
	    Bytes*		operator -> (void) const		{ return reinterpret_cast<Bytes*>(ptr); };
	    template<typename T>
		T&		ref(size_t o=0, size_t i=0)const	{ return reinterpret_cast<T*>(ptr+o)[i]; };
	    template<typename T>
				operator T& (void) const		{ return *reinterpret_cast<T*>(ptr); };

	    MemPtr		operator + (off_t i) const		{ return ptr+i; };
	    MemPtr		operator - (off_t i) const		{ return ptr-i; };
	    size_t		operator - (const MemPtr& mp)		{ return ptr-mp.ptr; };

	    MemPtr&		operator += (off_t i)			{ ptr += i; return *this; };
	    MemPtr&		operator -= (off_t i)			{ ptr -= i; return *this; };
	    MemPtr		operator ++ (int)			{ ptr += 1; return ptr-1; };
	    MemPtr&		operator ++ (void)			{ ptr += 1; return *this; };
	    MemPtr		operator -- (int)			{ ptr -= 1; return ptr+1; };
	    MemPtr&		operator -- (void)			{ ptr -= 1; return *this; };
    };

    struct CPU;

    class Machine {

	public:
	    byte_t*		mem;
	    size_t		mem_alloc;
	    int			fs_root;

	    std::vector<CPU*>	cpus;				// really not planning on multiprocessing but meh  :-)

	    int			stdin;
	    int			stdout;

	public:
	    uint_t		kmalloc(uint_t bytes);
	    bool		kfree(uint_t addr);
	    uint_t		salloc(uint_t bytes);
	    void		sfree(uint_t segid);

	    void		output(byte_t ch);
	    byte_t		input(void);
    };

}; // namespace Nightmare

#endif // Double inclusion guard
