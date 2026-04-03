#include "nasgr.hh"
#include "naslex.hh"

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

    std::ostream& operator << (std::ostream& os, const Loc& l)
    {
	os << (l.file? "filename": "(stream)") << ":" << l.begin.line << ":";
	os << l.begin.column;
	if(l.end.column > l.begin.column)
	    os << "-" << l.end.column;
	return os;
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
	    if(!s->op.empty()) {
	        std::cout << "--- Line " << s->line << ":";
	        if(!s->label.empty())
		    std::cout << " label:" << s->label;
		std::cout << " op:" << s->op;
		std::cout << " operands:" << s->operands.size() << std::endl;
	    }
	}
    }

};

class Lexer: public nasyy::Lexer {
    public:
        nas::Source&	src;

    public:
			Lexer(nas::Source& s): src(s), nasyy::Lexer(s.readline())	{ };

        virtual int wrap(void)
        {
	    if(src.eof())
		return 1;
	    in(src.readline());
	    return in().good()? 0: 1;
        };

};

int main(int, const char**)
{
    nas::Source		src;

    if(!src.setup("kernel.ns"))
	return 1;

    Lexer		lexer(src);
    nasyy::parser	parser(src.current, lexer);

    while(!src.eof()) {
	parser.parse();
    }
}

