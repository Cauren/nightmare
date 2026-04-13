
#include <cstddef>
#include <cstdint>
#include <string>
#include <list>
#include <vector>
#include <stack>
#include <sstream>
#include <format>

#ifndef NAS_PARSER_HH
#define NAS_PARSER_HH


class Instruction;
namespace nas {

    class InputFile;
    class SourceLine;

    struct Loc {
	InputFile*	file;
	struct {
		size_t	    line;
		size_t	    column;
	} begin, end;

	friend std::ostream& operator << (std::ostream& o, const Loc& l);
    };

    class Node {
	public:
	    enum Type {
		Nil, Error,
		Value, StringLit,
		Address, Seg, Binary, Unary, String, Register, Ibase, Index, EA,
		List, Line, Size,
	    };

	    enum EAType {
		Invalid, Immed, DRreg, PostInc, PreDec, Indirect, PreIndex, PostIndex,
	    };

	private:
	    struct Payload {
		Type			t_;
		union {
		    int64_t		val_;
		    EAType		ea_;
		};
		std::string		str_;
		std::vector<Node>	nodes_;
		Loc			loc_;

					Payload(void) = delete;
					Payload(const Loc& l, Type t=Nil): t_(t), val_(1), loc_(l)
										{ };
	    };


	private:
	    static std::list<Payload>	nodes;
	    Payload*			ptr;

	public:
					Node(void) = default;
					Node(nullptr_t): ptr(0)				{ };

	    static Node			make(const Loc& l, Type t, std::initializer_list<Node> nl = {});
	    Payload*			operator -> () const				{ return ptr; };
	    Node			add(const Node&) const;
	    Node			prefix(const Node&) const;
	    Node			set(int64_t v)					{ ptr->val_ = v; return *this; };
	    Node			set(std::string&& s)				{ ptr->str_ = s; return *this; };
	    Node			set(std::string& s)				{ ptr->str_ = std::move(s); return *this; };

	    Node&			operator = (nullptr_t)				{ ptr = nullptr; return *this; };

					operator bool (void) const			{ return ptr && ptr->t_!=Nil; };
	    bool			operator == (Node::Type t) const		{ return ptr && ptr->t_==t; };
	    bool			operator == (Node::EAType t) const		{ return ptr && ptr->ea_==t; };

	    size_t			begin(void) const				{ return ptr? ptr->loc_.begin.column: 0; };
	    size_t			end(void) const					{ return ptr? ptr->loc_.end.column: 0; };

	    Type			type(void) const				{ return ptr? ptr->t_: Nil; };
	    EAType			eatype(void) const				{ return ptr? ptr->ea_: Invalid; };
	    size_t			size(void) const				{ return ptr? ptr->nodes_.size(): 0; };
	    Node&			operator [] (size_t i) const			{ return ptr->nodes_[i]; };
	    std::vector<Node>&		nlist(void) const				{ return ptr->nodes_; };
	    std::string&		str(void) const					{ return ptr->str_; };
	    int64_t			val(void) const					{ return ptr->val_; };

	    static void			clear(void)					{ nodes.clear(); };
	    std::string			debug(void) const;
    };

    struct ParseError {
	Loc		loc;
	std::string	msg;
    };

    struct LineContext {
	InputFile*		file;
	size_t			line;
	std::vector<ParseError>	errs;
	SourceLine*		src;

	void			fin(const Node& root);
    };

    struct InputFile {
	std::string		name;
	size_t			line;
	FILE*			fd;
    };

    struct SourceLine {
	struct Pos {
	    size_t		    from = 0;
	    size_t		    to = 0;
	};
	struct Error: Pos {
	    std::string		    msg;
	};
	InputFile*		file;
	size_t			line;
	std::string		text;
	Instruction*		insn = nullptr;
	std::vector<Error>	errs;
	bool			fatal = false;
	Node			entire = nullptr;
	Node			label = nullptr;
	Node			op = nullptr;
	bool			labeled = false;
	std::vector<Node>	operands;

				SourceLine(InputFile* i, size_t ln, const std::string& t):
					file(i), line(ln), text(t)				{ };

	bool			err(const Node& n, std::string&& s) {
				    errs.emplace_back( Error{ n? n.begin(): 0, n? n.end(): 0, std::move(s) } );
				    return true;
				};
	template<typename... Args>
	bool			err(const Node& n, std::format_string<Args...> fmt, Args&&... args) {
				    return err(n, std::format(fmt, std::forward<Args>(args)...));
				};

	void			debug(void);
	void			print(bool);
    };

    struct Source {
	std::vector<InputFile>	files;
	std::vector<SourceLine>	lines;
	std::stack<LineContext>	ctx;
	LineContext*		current;

	bool			setup(const char* fname);
	bool			eof(void);
	const char*		readline(void);
    };

    bool parser(Source& src, int argc, const char** argv);

};


#endif // double inclusion

