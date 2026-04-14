
#include <format>
#include <iostream>
#include <fstream>
#include <vector>
#include <list>
#include <map>
#include <utility>
#include <cstring>
#include <unistd.h>

#include "nasparser.hh"


using nas::Node;
using nas::SourceLine;

struct Range_ {
    uint64_t			from;
    uint64_t			to;
    std::vector<uint16_t>	bytes;
};

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
    uint64_t			length = 0;
    std::list<Range_>		data;
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

struct Local {
    uint16_t			num;
    uint64_t			addr;
    Segment*			seg;
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

template<int bits> uint64_t signed_(int64_t n) {
    constexpr uint64_t sign = 1l << (bits-1);
    return (n<0)? (-n&(sign-1))|sign: n&(sign-1);
}

template<int bits> bool overflow_(int64_t n) {
    constexpr int64_t sign = 1l << (bits-1);
    return (n <= -sign) | (n >= sign);
}

struct EA {
    enum Type {
	DReg, Immediate, Address
    };
    Type			type;
    bool			resolved;
    Value::Relocate		reloc;
    uint32_t			eabits;
    std::vector<uint32_t>	eaext;

    void			word(uint32_t n)				{ eaext.emplace_back(n & 0777777); };
    void			sword(int32_t n)				{ word(signed_<18>(n)); };
    void			dword(uint64_t n)				{ word(n); word(n>>18); };
    void			sdword(int64_t n)				{ dword(signed_<36>(n)); };
};

struct Instruction;
struct Assembly {
    std::ostream&		object;
    std::ostream&		listing;

    nas::Source			source;

    std::vector<Segment*>	segs;
    std::multimap<uint16_t, Local>
				locals;
    std::map<std::string, Symbol>
				syms;

    Segment*			cseg = nullptr;

				Assembly(std::ostream& o, std::ostream& p=std::cerr): object(o), listing(p) { };

    bool			assemble(int, const char**);

    Value			eval(const Node&);
    uint16_t			ealen(const Node&);
    bool			ea(const Node& n, EA& ea, SourceLine& sl, bool unsized=false);
    Symbol*			find(const std::string& sym);
    Symbol*			make(const std::string& sym);
};

uint16_t Assembly::ealen(const Node& n)
{
    int ean = n.size();
    int osz = 0;
    if(ean>1 && n[ean-1]==Node::Size) {
	ean--;
	osz = n[ean].val();
    }

    Value v { 0 };

    switch(n.type()) {
      case Node::String:
      case Node::Address:
	if(n.size()>1 && n[1]==Node::Register)
	    return 4;
	return 8;
      case Node::Register:
	return 2;
    }
    if(n != Node::EA)
	return 0;

    const auto eat = n.eatype();

    switch(eat) {
      case Node::PostInc:
      case Node::PreDec:
	return 2;
      case Node::Immed: {
	  Value v = eval(n[0]);
	  if(!osz && v.unresolved || v.type != Value::Numeric)
	      osz = 4;
	  if(osz) switch(osz) {
	    case 1: if(!overflow_<6>(v.value)) break;
	    case 2: if(!overflow_<18>(v.value)) break;
	    case 4: if(!overflow_<36>(v.value)) break;
	    default:
		return 0;
	  } else if(!overflow_<6>(v.value)) {
	      osz = 1;
	  } else if(!overflow_<18>(v.value)) {
	      osz = 2;
	  } else {
	      osz = 4;
	  }
	  switch(osz) {
	    case 1:
	      return 2;
	    case 2:
	      return 4;
	    default:
	      return 6;
	  }
      }
    }

    if(ean<1 || n[0] != Node::Ibase)
	return 0;

    int64_t offset = 0;
    int areg = n[0].val();
    bool hasoffset = false;

    if(areg == 19)
	areg = 070;
    else
	areg &= 7;

    if(n[0].size()) {
	Value v = eval(n[0][0]);
	offset = v.value;
	hasoffset = true;
    }

    // two special cases:
    // (dx,ar) can reduce to either (ar), (d9,ar) or (d18,ar)
    // ([dx,ar]) can reduce to ([d9,ar])
    if(ean < 2) { // no index, no displacement, ar base
	if(areg<8 && offset==0 && eat==Node::Indirect)
	    return 2;
	if(!overflow_<9>(offset))
	    return 4;
	if(areg<8 && !overflow_<18>(offset))
	    return 4;
    }

    int64_t disp = 0;
    if(ean > 2) {
	Value v = eval(n[2]);
	disp = v.value;
	if(overflow_<9>(disp)) {
	    if(eat==Node::Indirect && !hasoffset && !overflow_<18>(disp)) {
		offset = disp;
		disp = 0;
	    }
	}
    }

    return offset? 6: 4;
}

bool Assembly::ea(const Node& n, EA& ea, SourceLine& sl, bool unsized)
{
    int ean = n.size();
    int osz = 0;
    if(ean>1 && n[ean-1]==Node::Size) {
	ean--;
	osz = n[ean].val();
    }
    int eam = 0200;
    switch(osz) {
      case 1: eam = 0000; break;
      case 2: eam = 0100; break;
    }
    if(unsized)
	eam = 0;

    // std::cerr << "EA: " << n.debug() << ", size " << osz << std::endl;

    Value v { 0 };
    uint32_t segnum = 0;

    ea.type = EA::Address;

    switch(n.type()) {
      case Node::Address:
	if(n == Node::Address) {
	    v = eval(n[0]);
	    if(n.size()>1 && n[1]==Node::Register) {
		if(overflow_<18>(v.value)) {
		    sl.err(n, "Value ({}) out of range", v.value);
		    return true;
		}
		ea.resolved = !v.unresolved;
		ea.reloc = Value::None;
		ea.eabits = eam | 050 | (n[1].val()&7);
		ea.sword(v.value);
		return false;
	    }
	}
	// fallthru
      case Node::String:
	ea.eabits = eam | 0072;

	v = eval(n);

	if(v.type != Value::Address) {
	    sl.err(n, "Value does not resolve to an address");
	}
	if(v.seg && v.seg->type==Segment::Literal)
	    segnum = v.seg->value;
	ea.resolved = !v.unresolved;
	ea.reloc = v.reloc;
	ea.word(segnum);
	ea.dword(v.value);
	return false;
      case Node::Register:
	ea.type = EA::DReg;
	ea.eabits = eam | (n.val()&7);
	return false;
    }
    if(n != Node::EA) {
	sl.err(n, "Invalid operand for {}", sl.op.str());
	return true;
    }

    const auto eat = n.eatype();

    switch(eat) {
      case Node::PostInc:
	ea.eabits = eam | 020 | (n[0].val()&7);
	return false;
      case Node::PreDec:
	ea.eabits = eam | 030 | (n[0].val()&7);
	return false;
      case Node::Immed: {
	  ea.type = EA::Immediate;
	  Value v = eval(n[0]);
	  if(v.unresolved || v.type != Value::Numeric) {
	      sl.err(n[0], "Only fully resolved numeric values are acceptable immediate operands");
	      osz = 4;
	  }
	  if(osz) switch(osz) {
	    case 1: if(!overflow_<6>(v.value)) break;
	    case 2: if(!overflow_<18>(v.value)) break;
	    case 4: if(!overflow_<36>(v.value)) break;
	    default:
		sl.err(n[0], "Value overflows explicit size");
		return true;
	  } else if(!overflow_<6>(v.value)) {
	      osz = 1;
	  } else if(!overflow_<18>(v.value)) {
	      osz = 2;
	  } else {
	      osz = 4;
	  }
	  switch(osz) {
	    case 1:
	      ea.eabits = 0300 | signed_<6>(v.value);
	      return false;
	    case 2:
	      ea.eabits = 0171;
	      ea.sword(v.value);
	      return false;
	    default:
	      ea.eabits = 0271;
	      ea.sdword(v.value);
	      return false;
	  }
      }
    }

    if(ean<1 || n[0] != Node::Ibase) {
	sl.err(n, "Internal: Indexed EA without an IBase?");
	return true;
    }
    int64_t offset = 0;
    int areg = n[0].val();
    bool hasoffset = false;

    if(areg == 19)
	areg = 070;
    else
	areg &= 7;

    if(n[0].size()) {
	Value v = eval(n[0][0]);
	if(v.type!=Value::Numeric || v.unresolved) {
	    sl.err(n[0][0], "Offset is not a constant numeric value");
	    return true;
	}
	offset = v.value;
	hasoffset = true;
    }

    // same two special cases as above:
    // (dx,ar) can reduce to either (ar), (d9,ar) or (d18,ar)
    // ([dx,ar]) can reduce to ([d9,ar])
    if(ean < 2) { // no index, no displacement, ar base
	if(areg<8 && offset==0 && eat==Node::Indirect) {
	    ea.eabits = eam | 020 | areg;
	    return false;
	}
	if(!overflow_<9>(offset)) {
	    ea.eabits = eam | 060 | areg;
	    if(eat == Node::Indirect)
		ea.word(signed_<9>(offset));
	    else
		ea.word(signed_<9>(offset)|0100000);
	    return false;
	}
	if(areg<8 && !overflow_<18>(offset)) {
	    ea.eabits = eam | 040 | areg;
	    ea.sword(offset);
	    return false;
	}
    }

    int64_t disp = 0;
    if(ean > 2) {
	Value v = eval(n[2]);
	if(v.type!=Value::Numeric || v.unresolved) {
	    sl.err(n[2], "Displacement is not a constant numeric value");
	    return true;
	}
	disp = v.value;
	if(overflow_<9>(disp)) {
	    // last resort, if not memory indexed _and_ no offset we can try there
	    if(eat==Node::Indirect && !hasoffset && !overflow_<18>(disp)) {
		offset = disp;
		disp = 0;
		hasoffset = true;
	    } else {
		sl.err(n[2], "Displacement out of range ({})", disp);
		return true;
	    }
	}
    }

    if(overflow_<18>(offset)) {
	sl.err(n[0], "Offset out of range ({})", offset);
	return true;
    }

    ea.eabits = eam | 0060 | areg;
    uint32_t extbits = 0;
    if(ean>1 && n[1]) {
	int scale = 0;
	if(n[1].size()) {
	    Value v = eval(n[1][0]);
	    if(v.type!=Value::Numeric || v.unresolved) {
		sl.err(n[1][0], "Scale is not a constant numeric value");
		return true;
	    }
	    switch(v.value) {
	      case 1: scale = 0; break;
	      case 2: scale = 1; break;
	      case 4: scale = 2; break;
	      case 8: scale = 3; break;
	      default:
		sl.err(n[1][0], "Scale must be 1, 2, 4 or 8");
		return true;
	    }
	}
	switch(eat) {
	  case Node::Indirect:	extbits = 0400000; break;
	  case Node::PreIndex:	extbits = 0500000; break;
	  case Node::PostIndex:	extbits = 0700000; break;
	}
	extbits |= scale << 12;
	extbits |= (v.value&7) << 9;
    } else switch(eat) {
      case Node::Indirect:	extbits = 0200000; break;
      case Node::PreIndex:	extbits = 0300000; break;
      case Node::PostIndex:
	sl.err(n, "Internal: postindexed without index?");
	return true;
    }

    extbits |= signed_<9>(disp);
    if(offset) {
	ea.word(extbits | (1<<14));
	ea.sword(offset);
    } else
	ea.word(extbits);
    
    return false;
}

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
		  v1.type = Value::Numeric;
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
	  if(n.str().c_str()[0] == '.') {
	      if(!cseg || !cseg->based)
		  return { std::format("Pseudo-label '{}' is only meaningful within a local segment", n.str()), &n };
	      if(n.str().c_str()[1] == 0)
		  return { Value::Address, false, int64_t(cseg->addr), cseg, Value::None };
	      char* fb;
	      uint16_t num = strtoul(n.str().c_str()+1, &fb, 10);
	      bool forward = (*fb=='f' || *fb=='F');
	      auto range = locals.equal_range(num);
	      if(forward) {
		  for(auto i = range.first; i!=range.second; i++)
		      if(i->second.seg==cseg && i->second.addr>=cseg->addr)
			  return { Value::Address, false, int64_t(i->second.addr), cseg, Value::None };
	      } else {
		  auto i = range.second;
		  while(i != range.first) {
		      i = std::prev(i);
		      if(i->second.seg==cseg && i->second.addr<cseg->addr)
			  return { Value::Address, false, int64_t(i->second.addr), cseg, Value::None };
		  }
	      }
	      return { Value::Address, true, 0, cseg, Value::None };
	  }
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

struct i_SEG: public Instruction {
    inline static Opcode::List<i_SEG> opcodes = {
	{ "SEG",	123 },
    };

    i_SEG(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool align(void)
    {
	return false;
    };

    bool pass1(Assembly& a)
    {
	Segment* seg = nullptr;

	if(src.operands.size() > 1)
	    return src.err(src.operands[1], "Too many arguments for SEG directive");

	if(src.label) {
	    src.labeled = false;
	    Symbol* sym = a.find(src.label.str());
	    if(sym)
		return src.err(src.label, "Multiply defined identifier '{}'", src.label.str());

	    if(src.operands.size() == 0) {

		sym = a.make(src.label.str());
		sym->type = Symbol::Seg;
		seg = sym->seg = a.segs.emplace_back(new Segment{ Segment::Relocatable, false, 0, src.label.str(), "", 0 });
		return false;

	    } else {

		if(src.operands[0] == Node::StringLit) {
		    sym = a.make(src.label.str());
		    sym->type = Symbol::Seg;
		    seg = sym->seg = a.segs.emplace_back(
			new Segment{ Segment::External, false, 0, src.label.str(), src.operands[0].str(), 0 }
		    );
		} else {
		    Value v = a.eval(src.operands[0]);
		    if(v.unresolved)
			return src.err(src.operands[0], "Literal SEG must be constant at the point of declaration");
		    switch(v.type) {
		      case Value::Numeric:
			sym = a.make(src.label.str());
			sym->type = Symbol::Seg;
			seg = sym->seg = a.segs.emplace_back(
			    new Segment{ Segment::Literal, false, uint32_t(v.value), src.label.str(), src.operands[0].str(), 0 }
			);
			sym->unresolved = false;
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
    inline static Opcode::List<i_ORG> opcodes = {
	{ "ORG",	0 },
    };

    i_ORG(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool align(void)
    {
	return false;
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
    };

};

struct i_EQU: public Instruction {
    inline static Opcode::List<i_EQU> opcodes = {
	{ "EQU",	0 },
    };

    i_EQU(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool align(void)
    {
	return false;
    };

    bool pass1(Assembly& a)
    {
	if(needs(1))
	    return true;
	if(!src.label)
	    return src.err(src.op, "EQU requires a label to define");
	src.labeled = false;
	Symbol* sym = a.find(src.label.str());
	if(sym)
	    return src.err(src.label, "Multiply defined identifier '{}'", src.label.str());
	Value v = a.eval(src.operands[0]);

	if(v.type == Value::Invalid)
	    return eerr(v);

	switch(v.type) {
	  case Value::Numeric:
	    sym = a.make(src.label.str());
	    sym->type = Symbol::Val;
	    sym->seg = nullptr;
	    break;
	  case Value::Seg:
	    sym = a.make(src.label.str());
	    sym->type = Symbol::Seg;
	    sym->seg = v.seg;
	    break;

	  case Value::Address:
	    sym = a.make(src.label.str());
	    sym->type = Symbol::Addr;
	    sym->seg = v.seg;
	    break;
	}

	sym->unresolved = v.unresolved;
	sym->value = uint64_t(v.value);
	return false;
    };

};

struct i_DS: public Instruction {
    inline static Opcode::List<i_DS> opcodes = {
	{ "DS",		0 },
    };

    i_DS(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool pass1(Assembly&)
    {
	return false;
    };

    bool pass2(Assembly&)
    {
	return false;
    };
};

struct i_DAT: public Instruction {
    inline static Opcode::List<i_DAT> opcodes = {
	{ "DW",		2 },
	{ "DL",		4 },
//	{ "DQ",		8 },
    };

    i_DAT(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

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
    inline static Opcode::List<i_DB> opcodes = {
	{ "DB",		1 },
    };

    i_DB(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

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
    inline static Opcode::List<i_DA> opcodes = {
	{ "DA",		6 },
    };

    i_DA(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

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

	    if(v.type == Value::Address && v.seg)
		segno = v.seg->value;

	    word(segno);
	    word(v.value);
	    word(v.value>>18);
	}
	return false;
    };

};

struct i_one_ea: public Instruction {
    EA			ea;

    i_one_ea(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool pass1(Assembly& a)
    {
	if(needs(1))
	    return true;
	if(!(ilen = a.ealen(src.operands[0])))
	    return true;
	return false;
    }

};

struct i_ea_reg: public i_one_ea {
    bool		regdest;
    int32_t		reg;

    i_ea_reg(SourceLine& sl, uint32_t b): i_one_ea(sl, b) { };

    bool pass1(Assembly& a)
    {
	if(needs(2))
	    return true;
	if((regdest = (src.operands[1] == Node::Register))) {
	    reg = src.operands[1].val();
	    if(!(ilen = a.ealen(src.operands[0])))
		return true;
	} else if(src.operands[0] == Node::Register) {
	    if(!(ilen = a.ealen(src.operands[1])))
		return true;
	} else
	    return src.err(src.op, "Impossible?  Src or dst must be a register");

	return false;
    };

};

struct i_EAR: public i_ea_reg {
    inline static Opcode::List<i_EAR> opcodes = {
	{ "MOV",	0400000 },
	{ "SEX",	0410000 },
	{ "ADD",	0420000 },
	{ "SUB",	0430000 },
	{ "ADC",	0440000 },
	{ "SBC",	0450000 },
	{ "AND",	0460000 },
	{ "OR",		0470000 },
	{ "XOR",	0500000 },
	{ "CMP",	0510000 },
	{ "BCLR",	0710000 },
	{ "BSET",	0720000 },
	{ "BTST",	0730000 },
	{ "ASR",	0740000 },
	{ "LSR",	0750000 },
	{ "ASL",	0760000 },
	{ "LSL",	0770000 },
	{ "LEA",	0220600 },
	{ "LDA",	0220500 },
	{ "LDS",	0220400 },
	{ "STA",	0220100 },
	{ "STS",	0220000 },
    };

    i_EAR(SourceLine& sl, uint32_t b): i_ea_reg(sl, b) { };

    bool pass2(Assembly& a)
    {
	if(a.ea(src.operands[regdest? 0: 1], ea, src, reg>7))
	    return true;

	if(!regdest)
	    ea.eabits |= 0400;
	ea.eabits |= (reg&7) << 9;
	word(bits | ea.eabits);
	for(const auto w: ea.eaext)
	    word(w);

	return false;
    };
};

struct i_UNARY: public i_one_ea {
    inline static Opcode::List<i_UNARY> opcodes = {
	{ "CLR",	0200000 },
	{ "TST",	0204000 },
	{ "INC",	0205000 },
	{ "DEC",	0206000 },
	{ "NEG",	0207000 },
	{ "COM",	0210000 },

	{ "SSMA",	0300400 },
	{ "SSML",	0300500 },
    };

    i_UNARY(SourceLine& sl, uint32_t b): i_one_ea(sl, b) { };

    bool pass2(Assembly& a)
    {
	bool unsized = true;

	if((bits>>15) == 2)
	    unsized = false;
	if(a.ea(src.operands[0], ea, src, unsized))
	    return true;

	word(bits | ea.eabits);
	for(const auto w: ea.eaext)
	    word(w);

	return false;
    };
};

struct i_ADDR: public i_one_ea {
    inline static Opcode::List<i_ADDR> opcodes = {
	{ "JSR",	0300100 },
	{ "JMP",	0300200 },
    };

    i_ADDR(SourceLine& sl, uint32_t b): i_one_ea(sl, b) { };

    bool pass2(Assembly& a)
    {
	if(a.ea(src.operands[0], ea, src, true))
	    return true;
	if(ea.type != EA::Address)
	    return src.err(src.operands[0], "{} requires an address operand", src.op.str());

	word(bits | ea.eabits);
	for(const auto w: ea.eaext)
	    word(w);

	return false;
    };
};

struct i_REL: public Instruction {
    inline static Opcode::List<i_REL> opcodes = {
	{ "BSR",	0000000 },
	{ "BRA",	0001000 },
	{ "BEQ",	0002000 },
	{ "BNE",	0003000 },
	{ "BLO",	0004000 },
	{ "BCC",	0004000 },
	{ "BHS",	0005000 },
	{ "BCS",	0005000 },
	{ "BLS",	0006000 },
	{ "BHI",	0007000 },
	{ "BMI",	0010000 },
	{ "BPL",	0011000 },
	{ "BLT",	0012000 },
	{ "BVS",	0012000 },
	{ "BGE",	0013000 },
	{ "BVC",	0013000 },
	{ "BLE",	0014000 },
	{ "BGT",	0015000 },
    };

    i_REL(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    bool pass1(Assembly& a)
    {
	if(needs(1))
	    return true;

	int osz = 1;
	if(src.operands[0].size() && src.operands[0][0] == Node::Size) {
	    osz = src.operands[0][0].val();
	} else {
	    // guess
	    Value v = a.eval(src.operands[0]);
	    if(!v.unresolved && a.cseg && abs((a.cseg->addr+2)-v.value) > 0377)
		    osz = 3;
	}

	ilen = 2+(osz-1);
	return true;
    };

    bool pass2(Assembly& a)
    {
	Value v = a.eval(src.operands[0]);
	if(v.unresolved || v.type != Value::Address || v.seg!=seg)
	    return src.err(src.operands[0], "Operand must resolve to an address within the same segment");
	int64_t disp = v.value - (addr+ilen);
	if(ilen==2) {
	    if(abs(disp)>0377)
		return src.err(src.operands[0], "Operand out of range of short branch");
	    word(bits | signed_<9>(disp));
	} else {
	    if(overflow_<27>(disp))
		return src.err(src.operands[0], "Operand out of range of long branch");
	    disp = signed_<27>(disp);
	    word(bits | (disp&0777));
	    word(disp>>9);
	}
	return false;
    };
};

struct i_IMM9: public Instruction {
    i_IMM9(SourceLine& sl, uint32_t b): Instruction(sl, b) { };

    inline static Opcode::List<i_IMM9> opcodes = {
	{ "RTS",	0041000 },
	{ "RTE",	0042000 },
	{ "TRAP",	0043000 },
    };

    bool pass1(Assembly& a)
    {
	if(src.operands.size() == 1) {
	    if(src.operands[0]!=Node::EA || src.operands[0].eatype()!=Node::Immed)
		return src.err(src.operands[0], "{} only accepts an immediate operand", src.op.str());
	} else if(needs(0))
	    return true;

	ilen = 2;
	return false;
    }

    bool pass2(Assembly& a)
    {
	int16_t	trap;
	if(src.operands.size() == 1) {
	    Value v = a.eval(src.operands[0][0]);
	    if(v.type!=Value::Numeric || v.unresolved)
		return src.err(src.operands[0][0], "Value must be a resolved constant");
	    if(v.value < 0 || v.value > 15)
		return src.err(src.operands[0][0], "Value ({}) out of range (0..15)", v.value);
	    trap = v.value;
	}
	word(bits | trap);
	return false;
    }
};

struct i_RLIST: public i_one_ea {
    i_RLIST(SourceLine& sl, uint32_t b): i_one_ea(sl, b) { };

    inline static Opcode::List<i_RLIST> opcodes = {
	{ "MOVM",	0300000 },
    };

    uint32_t	reglist;

    bool pass1(Assembly& a)
    {
	if(src.operands.size()<2)
	    return src.err(src.op, "More than one operand expected");
	reglist = 0;
	for(const auto o: src.operands) {
	    if(o == Node::Register) {
		reglist |= 1 << o.val();
	    } else {
		if(!(ilen = a.ealen(o)))
		    return true;
	    }
	}
	ilen += 2;
	return false;
    }

    bool pass2(Assembly& a)
    {
	if(a.ea(src.operands[
		(src.operands[0] == Node::Register)? src.operands.size()-1: 0
		], ea, src, true))
	    return true;

	word(bits | ea.eabits);
	for(const auto w: ea.eaext)
	    word(w);
	word(reglist);

	return false;
    }

};


static bool	printout = false;
static bool	debug = false;

void SourceLine::print(std::ostream& listing, bool reset = false)
{
    static Segment* lseg = nullptr;
    if(reset)
	lseg = nullptr;

    if(!errs.size() && !printout)
	return;

    for(const auto& e: errs) {
	listing << std::format("** ERROR:\t  {}:{}: {}", file->name, line, e.msg) << std::endl;
	listing << "**\t\t\t\t" << std::string(e.from, ' ') << "⬐" << std::string(e.to-e.from, '-') << std::endl;
    }

    if(insn && insn->bytes.size()>0) {
	if(lseg != insn->seg) {
	    lseg = insn->seg;
	    listing << "⮮[" << lseg->segname << "]" << std::endl;
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

	listing << std::format("   {:012o} {:<16s}{}", insn->addr, more(), text);
	while(b != insn->bytes.end())
	    listing << std::format("                {:<16s}", more()) << std::endl;
    } else {
	listing << "\t\t\t\t" << text;
    }
}

bool Assembly::assemble(int argc, const char** argv)
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

	    if(i->align() && (cseg->addr&1))
		cseg->addr++;

	    if(sl.labeled) {
		if(sl.label.str().c_str()[0] == '.') {
		    uint16_t num = std::atoi(sl.label.str().c_str()+1);
		    locals.emplace(num, Local{ num, cseg->addr, cseg });
		} else {
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
	    }

	    if(i->ilen) {
		if(!cseg) {
		    sl.err(sl.entire, "Fatal: No segment for code or data generation");
		} else if(!cseg->based) {
		    sl.err(sl.entire, "Fatal: No origin specified");
		} else if(cseg->type == Segment::External) {
		    sl.err(sl.entire, "Fatal: Code or data cannot be generated in external segments");
		} else {
		    i->seg = cseg;
		    i->addr = cseg->addr;

		    cseg->addr += i->ilen;
		}
	    }
	    if(sl.errs.size() > 0) {
		// pass 1 errors are fatal
		sl.print(listing);
		return 1;
	    }
	}

	if(cseg && cseg->based && cseg->addr > cseg->length)
	    cseg->length = cseg->addr;
    }

    // Pass 2
    bool fatal = false;
    cseg = nullptr;
    for(auto& sl: source.lines) {
	if(Instruction* i = sl.insn) {
	    cseg = i->seg;
	    if(!i->pass2(*this)) {
		if(i->ilen != i->bytes.size())
		    fatal = sl.err(sl.entire, "Fatal: Instruction length desync ({} vs {})", i->ilen, i->bytes.size());
	    }
	}
	sl.print(listing);
	if(fatal)
	    break;
    }

    // collect output
    for(auto& sl: source.lines)
	if(Instruction* i = sl.insn) {
	    if(!i->seg)
		continue;
	    Segment& seg = *i->seg;
	    uint64_t from = i->addr;
	    uint64_t to = i->addr+i->ilen;
	    if(seg.data.empty()) {
		seg.data.emplace_back(Range_{ from, to, i->bytes });
	    } else for(auto si = seg.data.begin(); si!=seg.data.end(); si++) {
		if((from<si->from && to>si->from) || (from>=si->from && from<si->to))
		    throw std::format("Range overlap seg {} offsets {}-{} with {}-{}", seg.segname, from, to, si->from, si->to);
		auto sn = std::next(si);
		if(from == si->to) {
		    si->bytes.insert(si->bytes.end(), i->bytes.begin(), i->bytes.end());
		    si->to = to;
		    if(sn != seg.data.end()) {
			if(to == sn->from) {
			    si->bytes.insert(si->bytes.end(), sn->bytes.begin(), sn->bytes.end());
			    si->to = sn->to;
			    seg.data.erase(sn);
			}
		    }
		    break;
		} else {
		    if(sn == seg.data.end()) {
			seg.data.insert(sn, Range_{from, to, i->bytes});
			break;
		    }
		    if(sn->from == to) {
			sn->bytes.insert(sn->bytes.begin(), i->bytes.begin(), i->bytes.end());
			sn->from = from;
			break;
		    }
		}
	    }
	}

    for(const auto& seg: segs) {
	object << std::format("SL{:06o}:{:09o}:{}", seg->value, seg->length, seg->segname);
	for(const auto& r: seg->data) {
	    int bytes = 0;
	    uint64_t addr = r.from;
	    for(const auto& b: r.bytes) {
		if(!bytes)
		    object << std::endl << std::format("DD{:9o}", addr);
		object << std::format(":{:03o}", b);
		if(++bytes > 29) {
		    addr += bytes;
		    bytes = 0;
		}
	    }
	}
	object << std::endl;
    }
    if(debug) for(auto& sl: source.lines)
	if(Instruction* i = sl.insn) {
	    if(!i->seg || !i->ilen)
		continue;
	    object << std::format("GS{:06o}:{:09o}:{:o}:{}:{}:{}",
		i->seg->value, i->addr, i->ilen,
		i->src.file->name, i->src.line, i->src.text);
	}

    return true;
}

int main(int argc, const char** argv)
{
    int opt;
    bool of = false;
    std::ostream* ofile = nullptr;

    while((opt = getopt(argc, (char*const*)(argv), "glo:")) >= 0)
	switch(opt) {
	  case 'g':
	    debug = true;
	    break;
	  case 'l':
	    printout = true;
	    break;
	  case 'o':
	    ofile = new std::ofstream(optarg, std::ios::trunc);
	    if(!*ofile) {
		std::cerr << std::format("{}: {}: {}", argv[0], optarg, std::strerror(errno)) << std::endl;
		return 1;
	    }
	    break;
	  default:
	    std::cerr << std::format("usage: {} [-l] [-o filename] source...", argv[0]) << std::endl;
	    return 1;
	}

    if(optind >= argc) {
	std::cerr << std::format("{}: no source files to assemble", argv[0]) << std::endl;
	return 1;
    }

    Assembly	as(ofile? *ofile: std::cout);

    as.assemble(argc-optind, argv+optind);

    if(ofile)
	delete ofile;
}

