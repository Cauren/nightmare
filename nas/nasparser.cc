#include <cstring>
#include <format>

#include "nasgr.hh"
#include "naslex.hh"
#include "nasparser.hh"
#include "nas.hh"

namespace nas {

    std::list<Node::Payload> Node::nodes;

    Node Node::make(const Loc& l, Type t, std::initializer_list<Node> nl)
    {
	Node n;
	n.ptr = &nodes.emplace_front(l, t);
	n.ptr->nodes_ = nl;
	return std::move(n);
    }

    Node Node::add(const Node& np) const
    {
	if(ptr)
	    ptr->nodes_.emplace(ptr->nodes_.end(), np);
	return *const_cast<Node*>(this);
    }

    std::string Node::debug(void) const
    {
	if(!ptr)
	    return "[nil]";

	static const char* tname[] = {
		"Nil", "Error",
		"Value",
		"Address", "Seg", "Binary", "Unary", "String", "Register", "Ibase", "Index", "EA", "List", "Line", "Size",
	};


	std::string out = std::format("[{}: {}", tname[ptr->t_], ptr->val_);
	if(ptr->str_.length())
	    out += std::format(" \"{}\"", ptr->str_);
	if(ptr->nodes_.size())
	    for(const auto& nn: ptr->nodes_)
		out += std::format(",{}", nn.debug());
	out += "]";
	return out;
    }

    std::ostream& operator << (std::ostream& os, const Loc& l)
    {
	os << (l.file? "filename": "(stream)") << ":" << l.begin.line << ":";
	os << l.begin.column;
	if(l.end.column > l.begin.column)
	    os << "-" << l.end.column;
	return os;
    }

    void SourceLine::debug(void)
    {
	std::cout << line << " > ";
	if(label.length())
	    std::cout << label << ": ";
	std::cout << op << " ";
	for(const auto& nn: operands)
	    std::cout << nn.debug() << " ";
	std::cout << std::endl;
    }

    bool Source::setup(const char* fname)
    {
	InputFile* ifile = &files.emplace_back(InputFile{fname, 0, nullptr});
	if(FILE* fd = fopen(fname, "r")) {
	    ifile->fd = fd;
	    current = &ctx.emplace(LineContext{ifile, 0});
	    return true;
	} else {
	   return false;
	}
    }

    bool Source::eof(void)
    {
	return ctx.empty();
    }

    const char* Source::readline(void)
    {
	while(!ctx.empty()) {

	    current = &ctx.top();
	    InputFile* i = current->file;

	    if((!i->fd) || feof(i->fd) || ferror(i->fd)) {
		if(i->fd)
		    fclose(i->fd);
		ctx.pop();
		continue;
	    }

	    current->line = ++i->line;
	    current->errs.clear();
	    current->src = nullptr;

	    char*	line = nullptr;
	    size_t	rlen;
	    ssize_t	len = getline(&line, &rlen, i->fd);

	    if(len > 0) {
		current->src = &lines.emplace_back(SourceLine{i, current->line, line});
	    }
	    free(line);
	    if(len <= 0)
		continue;

	    return current->src->text.c_str();
	}
	return nullptr;
    }

    void LineContext::fin(const Node& n) {
	SourceLine* s = src;
	for(const ParseError& pe: errs)
	    s->errs.emplace_back(SourceLine::Error{pe.loc.begin.column, pe.loc.end.column, std::move(pe.msg)});
	if(!n)
	    return;
	// std::cout << n.debug() << std::endl;
	if(n == Node::Line) {
	    if(n.size()>0 && n[0])
		s->label = n[0].str();
	    if(n.size()>1 && n[1])
		s->op = n[1].str();
	    if(n.size()>2 && n[2]) {
		if(n[2]==Node::List)
		    s->operands = std::move(n[2].nlist());
		else
		    s->operands.emplace_back(std::move(n[2]));
	    }
	}
    }

    class Lexer: public nasyy::Lexer {
        public:
            nas::Source&	src;

        public:
			Lexer(nas::Source& s): src(s), nasyy::Lexer()		{ };

	    virtual int wrap(void)
	    {
		if(src.eof())
		    return 1;
		in(src.readline());
		return in().good()? 0: 1;
	    };
    };

    bool parser(Source& src, int arg, int argc, const char** argv)
    {
	Lexer		lexer(src);
	nasyy::parser	parse(src.current, lexer);
	bool		first = true;

	// Fake inputfile to report errors
	InputFile* ifile = &src.files.emplace_back(InputFile{"<command line>", 0, nullptr});

	while(arg < argc) {
	    if(src.setup(argv[arg++])) {
		if(first)
		    lexer.in(src.readline());
		else
		    lexer.wrap();
		first = false;
		while(!src.eof())
		    if(parse.parse())
			return false;
	    } else {
		SourceLine& sl = src.lines.emplace_back(SourceLine{ifile, 0, ""});
	        sl.errs.emplace_back(SourceLine::Error{ size_t(arg), size_t(arg),
			std::format("Unable to read file '{}': {}", argv[argc-1], std::strerror(errno))
		});
		return false;
	    }
	}
	return true;
    };

};


