#include <vector>
#include <map>
#include <utility>

#ifndef NAS_ASM_HH
#define NAS_ASM_HH

namespace nas {

    struct Segment {
	enum Type {
	    Literal, External, Relocatable,
	};
	const Type		type;
	const int32_t		value;
	const std::string	filename;
    };

    struct Symbol {
	enum Type {
	    Seg, Addr, Val,
	};
	const Type		type;
	const std::string	name;
	Segment*		seg;
	int64_t			value;
    };

    struct Assembly {
	std::vector<Segment>		segs;
	std::map<std::string,Symbol>	syms;
    };


}; // namspace nas


#endif // double inclusion
