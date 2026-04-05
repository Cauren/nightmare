
#include <format>
#include <iostream>

#include "nasparser.hh"
#include "nas.hh"

using nas::SourceLine;

struct Opcode;

struct Code {
    typedef bool (Code::* Handler)(const Opcode&, const SourceLine&);

    bool		equ_handler(const Opcode&, const SourceLine&);
    bool		seg_handler(const Opcode&, const SourceLine&);
    bool		org_handler(const Opcode&, const SourceLine&);
    bool		data_handler(const Opcode&, const SourceLine&);
    bool		db_handler(const Opcode&, const SourceLine&);
    bool		da_handler(const Opcode&, const SourceLine&);
    bool		ead_handler(const Opcode&, const SourceLine&);
    bool		unary_handler(const Opcode&, const SourceLine&);
    bool		addr_handler(const Opcode&, const SourceLine&);
    bool		immed_handler(const Opcode&, const SourceLine&);
    bool		eaa_handler(const Opcode&, const SourceLine&);
    bool		aea_handler(const Opcode&, const SourceLine&);
    bool		reglist_handler(const Opcode&, const SourceLine&);
};

struct Opcode {
    const char* const	opcode;
    Code::Handler	handler;
    int32_t		bits;
};

struct Segment {
    enum Type {
	Literal, External, Relocatable,
    };
    const Type			type;
    const int32_t		value;
    const std::string		filename;
};

struct Symbol {
    enum Type {
	Seg, Addr, Val,
    };
    const Type			type;
    const std::string		name;
    Segment*			seg;
    int64_t			value;
};

struct Assembly {
    std::vector<Segment>	segs;
    std::map<std::string,Symbol>syms;
};


static const Opcode opcodes[] = {
	{ "SEG",	&Code::seg_handler },
	{ "ORG",	&Code::org_handler },
	{ "DB",		&Code::db_handler,	1 },
	{ "DW",		&Code::data_handler,	2 },
	{ "DL",		&Code::data_handler,	4 },
	{ "DQ",		&Code::data_handler,	8 },
	{ "DA",		&Code::da_handler,	6 },

	{ "MOV",	&Code::ead_handler,	0400000 },
	{ "ADD",	&Code::ead_handler,	0410000 },
	{ "SUB",	&Code::ead_handler,	0420000 },
	{ "LEA",	&Code::eaa_handler,	0600600 },
	{ "LDA",	&Code::eaa_handler,	0600500 },
	{ "LDS",	&Code::eaa_handler,	0600400 },
	{ "STA",	&Code::aea_handler,	0610500 },
	{ "STS",	&Code::aea_handler,	0610400 },

	{ "CLR",	&Code::unary_handler,	0600000 },
	{ "DEC",	&Code::unary_handler,	0601000 },
	{ "JSR",	&Code::addr_handler,	0700000 },
	{ "JMP",	&Code::addr_handler,	0700100 },

	{ "TRAP",	&Code::immed_handler,	0 },

	{ "PUSH",	&Code::reglist_handler,	0 },
	{ "PULL",	&Code::reglist_handler, 0 },
};

bool Code::equ_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::seg_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::org_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::data_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::db_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::da_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::ead_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::unary_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::addr_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::immed_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::eaa_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::aea_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}

bool Code::reglist_handler(const Opcode& op, const SourceLine& sl)
{
    return true;
}



nas::Source src;

int main(int argc, const char** argv)
{

    nas::parser(src, 1, argc, argv);
    for(auto& sl: src.lines)
	sl.debug();

}

