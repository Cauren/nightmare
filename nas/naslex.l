%top {

#include <cuchar>
#include "nasgr.hh"

#define Token(T) return parser::token::TOK_##T
#define VToken(T,N,V) yylval = nas::Node::make( location(), nas::Node::N).set(V); Token(T)

namespace nasyy {
typedef nas::Loc location;
}

}

%option outfile="naslex.cc"
%option c++
%option unicode
%option bison-cc
%option bison-locations
%option bison-cc-namespace=nasyy
%option namespace=nasyy
%option lexer=Lexer
%option header-file="naslex.hh"
%option YYLTYPE=nas::Loc

%{
	static std::string tmpstr;

	static int64_t deutf8(const char* txt, size_t len) {
	    std::mbstate_t mbs{};
	    char32_t ch = 0xFFFD;
	    std::mbrtoc32(&ch, txt, len, &mbs);
	    return ch;
	}

%}

ISTART		[_\p{UnicodeIdentifierStart}]
ID		[_\p{UnicodeIdentifierPart}]
NOID		\P{UnicodeIdentifierPart}
SPACE		[ \t]

%x STRING MNEM

%%

<*>;.*					/* skip */
<*>^{SPACE}*\*.*			/* skip */

<*>[dD][0-7](?={NOID}|\R)		VToken(DREG, Value, text()[1] - 48);
<*>[aA][0-7](?={NOID}|\R)		VToken(AREG, Value, text()[1] - 40);
<*>[sS][pP](?={NOID}|\R)		VToken(AREG, Value, 7);
<*>[pP][cC][rR]?(?={NOID}|\R)		Token(PCR);
<*>[cC][cC][rR]?(?={NOID}|\R)		Token(CCR);
<*>[iI][rR](?={NOID}|\R)		Token(IR);
<*>[sS][mM][rR]?(?={NOID}|\R)		Token(SMR);

^{ISTART}{ID}*				start(MNEM); VToken(LABEL, String, str());
^\.[0-9]+				start(MNEM); VToken(LABEL, String, str());
^{SPACE}+				start(MNEM);

<*>{SPACE}*\R				start(INITIAL); Token(EOL);
<*>{SPACE}+				/* nothing */

<MNEM>{ISTART}{ID}*			start(INITIAL); VToken(OP, String, str());

{ISTART}{ID}*				VToken(IDENT, String, str());
\.[0-9]+[bBfF]				VToken(IDENT, String, str());
\.[bwlaqBWLAQ]				VToken(SIZE, Value, 0);

<<					Token(LSHIFT);
>>					Token(RSHIFT);

\$[0-9a-fA-F]+				VToken(LITERAL, Value, std::strtol(text()+1, 0, 16));
[0-9]+					VToken(LITERAL, Value, std::strtol(text(), 0, 10));
@[0-7]+					VToken(LITERAL, Value, std::strtol(text()+1, 0, 8));
%[01]+					VToken(LITERAL, Value, std::strtol(text()+1, 0, 2));
'\p{Unicode}				VToken(LITERAL, Value, deutf8(text()+1, size()-1));

<STRING>[^"\\\n]+			tmpstr += str();
<STRING>\\.				tmpstr += text()[0];
<STRING>["\n]				pop_state(); VToken(STRING, String, tmpstr);
<INITIAL>\"[^"\\\n]+			push_state(STRING); tmpstr = str().substr(1);

<INITIAL,MNEM>\p{ASCII}			return text()[0];
<INITIAL,MNEM>\p{Non_ASCII_Unicode}+	Token(BAD);
<*>.					Token(BAD);
<*><<EOF>>				Token(EOF);

%%

