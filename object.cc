#include <fstream>
#include <sstream>
#include "object.hh"

namespace Nightmare {


bool Object::load(std::istream& in)
{
    std::string line;
    Segment*	cs;

    while(std::getline(in, line).good()) {
	char		cmd[2];
	uword_t		seg;
	uint_t		addr;
	std::string	text;

	std::istringstream il(line);

	il >> cmd[0] >> cmd[1] >> std::oct;

	/*  */ if(cmd[0]=='S' && cmd[1]=='L') {
	    il >> seg >> addr >> text;
	    cs = &segs.emplace_back(Segment{ text, seg, addr });
	} else if(cmd[0]=='D' && cmd[1]=='D') {
	    il >> addr;
	    Data& d = cs->data.emplace_back(Data{ addr });
	    while(il.good()) {
		il >> seg;
		d.bytes.emplace_back(seg);
	    }
	} else if(cmd[0]=='G' && cmd[1]=='S') {
	    int	len, line;
	    char space;
	    std::string file;
	    il >> seg >> addr >> len >> file >> std::dec >> line;
	    il.get(); // skip the one space
	    std::getline(il, text);
	    slines.emplace(SourceLine{ addr, seg, uword_t(len), text });
	} else if(cmd[0]=='G' && cmd[1]=='Y') {
	    il >> seg >> addr >> text;
	    syms.emplace(Symbol{ addr, seg, text });
	}
    }

    return true;
}

const Object::Symbol& Object::symbol(uword_t seg, uint_t addr)
{
    auto big = syms.upper_bound(Symbol{ addr, seg });
    if(big != syms.begin())
	--big;
    return *big;
}

const Object::SourceLine& Object::source(uword_t seg, uint_t addr)
{
    auto big = slines.upper_bound(SourceLine{ addr, seg });
    if(big != slines.begin())
	--big;
    return *big;
}



}; // namespace Nightmare

