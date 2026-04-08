
#include <format>
#include <iostream>
#include <vector>
#include <map>
#include <utility>

#include "nasparser.hh"


using nas::Node;

struct Segment {
    enum Type {
	Literal, External, Relocatable,
    };
    Type			type;
    bool			based;
    const uint32_t		value;
    const std::string		segname;
    const std::string		filename;
    uint64_t			addr;
};

struct Symbol {
    enum Type {
	Seg, Addr, Val,
    };
    const std::string		name;
    Type			type = Val;
    bool			unresolved = true;
    Segment*			seg = nullptr;
    uint64_t			value = 0;
};

struct Value {
    enum Type {
	Invalid, Numeric, Seg, Address,
    };
    enum Relocate {
	None, SegRel, AddrRel, BothRel,
    };
    Type			type = Invalid;
    bool			unresolved = false;
    int64_t			value = 0;
    Segment*			seg = nullptr;
    Relocate			reloc = None;
    std::string			err;
    const Node*			err_node = nullptr;

				Value(int64_t v=0): type(Numeric), value(v)			{ };
				Value(std::string&& e, const Node* n):
				    type(Invalid), err(e), err_node(n)	{ };
				Value(Type t, bool u, int64_t a, Segment* s=nullptr, Relocate r=None):
				    type(t), unresolved(u), value(a), seg(s), reloc(r)		{ };
				Value(Value&&) = default;
				Value(const Value&) = default;
				Value(void) = default;

    Value&			operator = (Value&&) = default;
    Value&			operator = (const Value&) = default;
};

struct EA {
    enum Type {
	DReg, Immediate, Address
    };
    Type			type;
    bool			resolved;
    Value::Relocate		reloc;
    uint32_t			eabits;
    std::vector<uint32_t>	eaext;
};

struct Instruction;
struct Assembly {
    nas::Source			source;

    std::vector<Segment>	segs;
    std::map<std::string,Symbol>syms;

    Segment*			cseg = nullptr;

    bool			pass1(int, const char**);

    Value			eval(const Node&);
    Symbol*			find(const std::string& sym);
    Symbol*			make(const std::string& sym);
};

Symbol* Assembly::find(const std::string& sym)
{
    try {
	return &syms.at(sym);
    } catch(std::out_of_range) {
	if(cseg)
	    try {
		return &syms.at(std::format("<>:<>", cseg->segname, sym));
	    } catch(std::out_of_range) {
		;
	    }
    }
    return nullptr;
}

Symbol* Assembly::make(const std::string& sym)
{
    const auto [s, ret] = syms.emplace(sym, Symbol{ sym });
    if(ret)
	return &s->second;
    return nullptr;
}

Value Assembly::eval(const Node& n)
{
    switch(n.type()) {

      case Node::Value:
	return { n.val() };

      case Node::Binary: {
	  Value	v1 = eval(n[0]), v2 = eval(n[1]);
	  if(v1.type==Value::Invalid)
	      return v1;
	  if(v2.type==Value::Invalid)
	      return v2;
	  if(v1.type==Value::Address && v2.type==Value::Address) {
	      if(n.val() == '-') {
		  v1.unresolved |= v2.unresolved;
		  if(v1.unresolved)
		      return v1;
		  if(v1.seg == v2.seg) {
		      v1.value -= v2.value;
		      return v1;
		  }
	      }
	      return { "Substraction is the only allowed operation between two addresses", &n };
	  }
	  if(v2.type==Value::Address && v1.type==Value::Numeric && n.val()=='+') {
	      Value vt = v2;
	      v2 = v1;
	      v1 = vt;
	  }
	  if(v1.type==Value::Address) {
	      if(v2.type == Value::Numeric and n.val()=='+') {
		  v1.value += v2.value;
		  v1.unresolved |= v2.unresolved;
	      } else if(v2.type == Value::Numeric and n.val()=='-') {
		  v1.value -= v2.value;
		  v1.unresolved |= v2.unresolved;
	      } else {
		  return { std::format("Invalid operands for operator '{}'", char(n.val())), &n };
	      }
	      return v1;
	  }
	  if(v1.type==Value::Seg)
	      return { "Operations on segments are not permitted", &n[0] };
	  if(v2.type==Value::Seg)
	      return { "Operations on segments are not permitted", &n[1] };
	  switch(n.val()) {
	    case '+':	v1.value += v2.value; break;
	    case '-':	v1.value -= v2.value; break;
	    case '*':	v1.value *= v2.value; break;
	    case '/':	v1.value /= v2.value; break;
	    case '%':	v1.value %= v2.value; break;
	    case '|':	v1.value |= v2.value; break;
	    case '&':	v1.value &= v2.value; break;
	    case '^':	v1.value ^= v2.value; break;
	    default:
	     return { std::format("Operator {} unknown", n.val()), &n };
	  }
	  return v1;
      }

      case Node::Unary: {
	  Value v1 = eval(n[0]);
	  if(v1.type != Value::Numeric) {
	      return { std::format("Operator '{}' requires a numeric operand", char(n.val())), &n[1] };
	  }
	  switch(char(n.val())) {
	    case '-':	v1.value = -v1.value; break;
	    case '~':	v1.value = ~v1.value; break;
	  }
	  return v1;
      }

      case Node::Address: {
	  if(n.size()<1)
	      break;
	  Value v1 = eval(n[0]);
	  if(n.size()>1) {
	      Symbol* sym = find(n[1].str());
	      if(!sym || sym->type != Symbol::Seg)
		  return { std::format("Qualifier '{}' is not a segment name", n[1].str()), &n[1] };
	      return { Value::Address, v1.unresolved, v1.value, sym->seg,
		  (sym->seg && sym->seg->type==Segment::Relocatable)? Value::AddrRel: Value::None };
	  }
	  return { Value::Address, v1.unresolved, v1.value, nullptr };
      }

      case Node::String: {
	  Symbol* sym = find(n.str());
	  if(!sym)
	      return { Value::Numeric, true, 0 };
	  switch(sym->type) {
	    case Symbol::Val:
	      return { Value::Numeric, sym->unresolved, int64_t(sym->value) };
	    case Symbol::Seg:
	      return { Value::Seg, false, int64_t(sym->value), sym->seg,
		  (sym->seg && sym->seg->type==Segment::Relocatable)? Value::SegRel: Value::None };
	    case Symbol::Addr:
	      return { Value::Address, sym->unresolved, int64_t(sym->value), sym->seg, 
		  (sym->seg && sym->seg->type==Segment::Relocatable)? Value::AddrRel: Value::None };
	  }
      }

      default:
	break;
    }
    return { "Unspecific evaluation error (not implemented?)", &n };
}

using nas::SourceLine;

struct Instruction {
    SourceLine&			src;
    uint32_t			bits;

    Segment*			seg = nullptr;
    uint64_t			addr = 0;
    size_t			ilen = 0;
    bool			words = true;
    std::vector<uint16_t>	bytes;

				Instruction(SourceLine& s, uint32_t b): src(s), bits(b)	{ };

    virtual bool		pass1(Assembly&)				{ return false; };
    virtual bool		pass2(Assembly&)				{ return false; };
    virtual bool		align(void)					{ return true; };

    bool			needs(int num_args);
    bool			codegen(Assembly&, bool align=true);
    bool			eerr(const Value&);				// always returns true
    void			byte(uint16_t n)				{ bytes.emplace_back(n&0777); };
    void			word(uint32_t n)				{ byte(n>>9); byte(n); };
};

bool Instruction::needs(int num_args)
{
    if(src.operands.size() < num_args) {
	src.err(src.op, "No operands supplied to {}", src.op.str());
	return true;
    }
    if(src.operands.size() > num_args) {
	src.err(src.operands[num_args], "Too many operands for {}", src.op.str());
	return true;
    }
    return false;
}

bool Instruction::eerr(const Value& v)
{
    if(v.type == Value::Invalid)
	src.err(v.err_node? *v.err_node: src.entire, "Invalid operand: {}", v.err);
    return true;
}

struct Opcode {
    struct Data;

    const char* const		op;
    int32_t			bits;
    typedef Instruction*	(Maker)(SourceLine&, uint32_t);

    struct Data {
        int32_t			bits;
	Maker*			maker;
    };

    inline static std::map<std::string, const Data> opcodes;
    template<typename H>
	    static Instruction* make_(SourceLine& sl, uint32_t b) {
		return new H{sl, b};
	    };

    template<typename IT> struct List {
	List(std::initializer_list<Opcode> ops) {
	    for(auto o: ops)
		opcodes.try_emplace(std::string(o.op), Data{ o.bits, &(make_<IT>) });
	};
    };
};

#define OPCODES(OP) OP(SourceLine& sl, uint32_t b): Instruction(sl, b) { }; \
    inline static Opcode::List<OP> opcodes

struct i_SEG: public Instruction {
    OPCODES(i_SEG) = {
	{ "SEG",	123 },
    };

    bool pass1(Assembly& a)
    {
	Segment* seg = nullptr;

	if(src.operands.size() > 1)
	    return src.err(src.operands[1], "Too many arguments for SEG directive");

	if(src.label) {
	    Symbol* sym = a.find(src.label.str());
	    if(sym)
		return src.err(src.label, "Multiply defined identifier '{}'", src.label.str());

	    if(src.operands.size() == 0) {

		sym = a.make(src.label.str());
		sym->type = Symbol::Seg;
		seg = sym->seg = &a.segs.emplace_back(Segment{ Segment::Relocatable, false, 0, src.label.str(), "", 0 });
		return false;

	    } else {

		if(src.operands[0] == Node::StringLit) {
		    sym = a.make(src.label.str());
		    sym->type = Symbol::Seg;
		    seg = sym->seg = &a.segs.emplace_back(
			Segment{ Segment::External, false, 0, src.label.str(), src.operands[0].str(), 0 }
		    );
		} else {
		    Value v = a.eval(src.operands[0]);
		    if(v.unresolved)
			return src.err(src.operands[0], "Literal SEG must be constant at the point of declaration");
		    switch(v.type) {
		      case Value::Numeric:
			sym = a.make(src.label.str());
			sym->type = Symbol::Seg;
			seg = sym->seg = &a.segs.emplace_back(
			    Segment{ Segment::Literal, false, 0, src.label.str(), src.operands[0].str(), 0 }
			);
			break;
		      case Value::Seg:
		      case Value::Address:
			sym = a.make(src.label.str());
			sym->type = Symbol::Seg;
			seg = sym->seg = v.seg;
			break;
		      default:
			return src.err(src.operands[0], "Literal SEG must resolve to a segment value");
		    }
		}
	    }

	} else if(src.operands.size() == 1) {

	    Value v = a.eval(src.operands[0]);
	    if(v.unresolved || v.type != Value::Seg || !v.seg)
		return src.err(src.operands[0], "SEG directive requires a segment argument");
	    seg = v.seg;

	} else
	    return src.err(src.op, "SEG directive requires exactly one argument");

	if(seg)
	    a.cseg = seg;

	return false;
    };
};

struct i_ORG: public Instruction {
    OPCODES(i_ORG) = {
	{ "ORG",	0 },
    };

    bool pass1(Assembly& a)
    {
	if(needs(1))
	    return true;

	Value v = a.eval(src.operands[0]);
	if(v.type==Value::Invalid)
	    return eerr(v);
	if(v.unresolved)
	    return src.err(src.operands[0], "ORG operand must resolve to a constant address or value");
	if(v.type == Value::Seg)
	    return src.err(src.operands[0], "ORG operand cannot be a bare segment");
	Segment* seg = a.cseg;
	if(v.type==Value::Address && v.seg)
	    seg = v.seg;
	if(!seg)
	    return src.err(src.operands[0], "Cannot set origin outside a segment");
	if(v.type==Value::Address || v.type==Value::Numeric) {
	    seg->addr = v.value;
	    seg->based = true;
	    a.cseg = seg;
	}
	return false;
    }

};

struct i_EQU: public Instruction {
    OPCODES(i_EQU) = {
	{ "EQU",	0 },
    };

    bool pass1(Assembly& a)
    {
	if(needs(1))
	    return true;
	if(!src.label)
	    return src.err(src.op, "EQU requires a label to define");
	Value v = a.eval(src.operands[0]);
	switch(v.type) {
	  case Value::Invalid:
	    return eerr(v);
	}
	return false;
    };

};

struct i_DAT: public Instruction {
    OPCODES(i_DAT) = {
	{ "DW",		2 },
	{ "DL",		4 },
//	{ "DQ",		8 },
    };

    bool pass1(Assembly&)
    {
	ilen = src.operands.size() * bits;
	return false;
    };

    bool pass2(Assembly& a)
    {
	for(const auto n: src.operands) {
	    Value	v = a.eval(n);
	    bool	neg = v.value < 0;
	    int64_t	val = neg? -v.value: v.value;

	    if(v.type == Value::Invalid)
		eerr(v);

	    switch(bits) {
	      case 2:
		val &= (1<<17)-1;
		if(neg)
		    val |= (1<<17);
		word(val);
		break;
	      case 4:
		val &= (1l<<35)-1;
		if(neg)
		    val |= (1l<<35);
		word(val);
		word(val>>18);
		break;
	      default:
		src.err(n, "Don't know this?");
		break;
	    }
	}
	return false;
    };

};

struct i_DB: public Instruction {
    OPCODES(i_DB) = {
	{ "DB",		1 },
    };

    bool align(void)
    {
	return false;
    };

    bool pass1(Assembly&)
    {
	ilen = 0;
	for(const auto n: src.operands) {
	    if(n == Node::StringLit)
		ilen += n.str().size();
	    else
		ilen++;
	}
	return false;
    };

    bool pass2(Assembly& a)
    {
	words = false;
	for(const auto n: src.operands) {
	    if(n == Node::StringLit) {
		for(char c: n.str())
		    bytes.emplace_back(c);
	    } else {
		Value v = a.eval(n);
		if(v.type == Value::Invalid)
		    eerr(v);
		int16_t val = v.value;
		if(v.type == Value::Numeric) {
		    if(v.value < 0) {
			val = -val;
			val |= 0400;
		    } else
			val = val & 0377;
		}
		byte(val);
	    }
	}
	return false;
    };

};

struct i_DA: public Instruction {
    OPCODES(i_DA) = {
	{ "DA",		6 },
    };

    bool pass1(Assembly&)
    {
	ilen = src.operands.size() * 6;
	return false;
    };

    bool pass2(Assembly& a)
    {
	for(const auto n: src.operands) {
	    Value	v = a.eval(n);
	    bool	neg = v.value < 0;
	    int64_t	val = neg? -v.value: v.value;

	    if(v.type == Value::Invalid)
		eerr(v);
	    int32_t	segno = 0;

	    word(segno);
	    word(v.value);
	    word(v.value>>18);
	}
	return false;
    };

};

struct i_EAD: public Instruction {
    OPCODES(i_EAD) = {
	{ "MOV",	0400000 },
	{ "ADD",	0410000 },
	{ "SUB",	0420000 },
	{ "LEA",	0600600 },
    };
};

struct i_EAA: public Instruction {
    OPCODES(i_EAA) = {
	{ "LDA",	0600500 },
	{ "LDS",	0600400 },
	{ "STA",	0610500 },
	{ "STS",	0610400 },
    };
};

struct i_UNARY: public Instruction {
    OPCODES(i_UNARY) = {
	{ "CLR",	0600000 },
	{ "DEC",	0601000 },
    };
};

struct i_ADDR: public Instruction {
    OPCODES(i_ADDR) = {
	{ "JSR",	0700000 },
	{ "JMP",	0700100 },
    };
};

struct i_IMM9: public Instruction {
    OPCODES(i_IMM9) = {
	{ "TRAP",	0 },
    };
};

struct i_RLIST: public Instruction {
    OPCODES(i_RLIST) = {
	{ "PUSH",	0 },
	{ "PULL",	0 },
    };
};


/*
|-------|-------|-------|-------
** ERROR:       filename.ns:line:      
/- [SEGMENT]
   000000000000 000 000 000 000 
   000000000010 000000 000000
*/

void SourceLine::print(bool reset = false)
{
    static Segment* lseg = nullptr;
    if(reset)
	lseg = nullptr;

    if(errs.size())
	debug();
    for(const auto& e: errs) {
	std::cout << std::format("** ERROR:\t  {}:{}: {}", file->name, line, e.msg) << std::endl;
	std::cout << "**\t\t\t\t" << std::string(e.from, ' ') << "⬐" << std::string(e.to-e.from, '-') << std::endl;
    }

    if(insn && insn->bytes.size()>0) {
	if(lseg != insn->seg) {
	    lseg = insn->seg;
	    std::cout << "⮮[" << lseg->segname << "]" << std::endl;
	}

	auto b = insn->bytes.begin();
	auto more = [&](void) {
	    std::string rv;
	    if(insn->words) {
		for(int i=0; i<2 && b!=insn->bytes.end(); i++) {
		    rv += std::format("{:03o}{:03o} ", b[0], b[1]);
		    advance(b, 2);
		}
	    } else {
		for(int i=0; i<4 && b!=insn->bytes.end(); i++) {
		    rv += std::format("{:03o} ", b[0]);
		    advance(b, 1);
		}
	    }
	    return rv;
	};

	std::cout << std::format("   {:012o} {:<15s} {}", insn->addr, more(), text);
	while(b != insn->bytes.end())
	    std::cout << std::format("                {:<15s}", more()) << std::endl;
    } else {
	std::cout << "\t\t\t\t" << text;
    }
}

bool Assembly::pass1(int argc, const char** argv)
{
    if(!nas::parser(source, argc, argv))
	return false;

    // Parse and first pass
    for(auto& sl: source.lines) {
	// sl.debug();
	Instruction* i = nullptr;
	try {
	    if(sl.op) {
	        auto od = Opcode::opcodes.at(sl.op.str());
	        i = od.maker(sl, od.bits);
	    }
	} catch(std::out_of_range) {
	    sl.err(sl.op, "Unknown mnemonic '{}'", sl.op.str());
	    continue;
	}
	sl.insn = i;

	if(i) {
	    i->pass1(*this);
	    if(i->ilen) {
		if(!cseg) {
		    sl.err(sl.entire, "Fatal: No segment for code or data generation");
		} else if(!cseg->based) {
		    sl.err(sl.entire, "Fatal: No origin specified");
		} else if(cseg->type == Segment::External) {
		    sl.err(sl.entire, "Fatal: Code or data cannot be generated in external segments");
		} else {
		    if(i->align() && (cseg->addr&1))
			cseg->addr++;
		    i->seg = cseg;
		    i->addr = cseg->addr;

		    if(sl.label) {
			Symbol* sym = find(sl.label.str());
			if(sym)
			    sl.err(sl.label, "Multiply defined identifier '{}'", sl.label.str());
			else {
			    sym = make(sl.label.str());
			    sym->type = Symbol::Addr;
			    sym->unresolved = false;
			    sym->seg = cseg;
			    sym->value = cseg->addr;
			}

		    }

		    cseg->addr += i->ilen;
		}
	    }
	    if(sl.errs.size() > 0) {
		// pass 1 errors are fatal
		sl.print();
		return 1;
	    }
	}
    }

    // Pass 2
    bool fatal = false;
    for(auto& sl: source.lines) {
	if(Instruction* i = sl.insn) {
	    i->pass2(*this);
	    if(i->ilen != i->bytes.size())
		fatal = sl.err(sl.entire, "Fatal: Instruction length desync");
	}
	sl.print();
	if(fatal)
	    break;
    }

    return true;
}

int main(int argc, const char** argv)
{
    Assembly	as;

    as.pass1(argc-1, argv+1);
}

