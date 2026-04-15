#include <cstdint>
#include <cstddef>
#include <iostream>
#include <string>
#include <set>
#include <vector>
#include <compare>
#include "cpu.hh"

#ifndef NIGHTMARE_OBJECT_HH__
#define NIGHTMARE_OBJECT_HH__

namespace Nightmare {



    struct Object
    {
	struct Data {
	    uint_t		addr;
	    std::vector<byte_t>	bytes;
	};

	struct Segment {
	    std::string		name;
	    uword_t		value;
	    uint_t		size;
	    std::vector<Data>	data;
	};

	struct SourceLine {
	    uint_t		addr;
	    uword_t		seg;
	    uword_t		len;
	    std::string		text;

	    std::weak_ordering	operator <=> (const SourceLine& l) const {
		if(seg<l.seg || (seg==l.seg && addr<l.addr))
		    return std::weak_ordering::less;
		if(seg==l.seg && addr==l.addr)
		    return std::weak_ordering::equivalent;
		return std::weak_ordering::greater;
	    };
	};

	struct Symbol {
	    uint_t		addr;
	    uword_t		seg;
	    std::string		text;

	    std::weak_ordering	operator <=> (const Symbol& l) const {
		if(seg<l.seg || (seg==l.seg && addr<l.addr))
		    return std::weak_ordering::less;
		if(seg==l.seg && addr==l.addr)
		    return std::weak_ordering::equivalent;
		return std::weak_ordering::greater;
	    };
	};

	std::set<SourceLine>	slines;
	std::set<Symbol>	syms;
	std::vector<Segment>	segs;

	bool			load(std::istream&);
	const SourceLine&	source(uword_t seg, uint_t addr);
	const Symbol&		symbol(uword_t seg, uint_t addr);
    };


}; // namespace Nightmare

#endif // Double inclusion guard
