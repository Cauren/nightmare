%top {

#include <cuchar>
#include <utf8proc.h>
#include "nasgr.hh"

#define Token(T) return parser::token::TOK_##T
#define VToken(T,N,V) yylval = nas::Node::make( yylloc, nas::Node::N).set(V); Token(T)

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
	static size_t tmpstr_col;

	static int64_t deutf8(const char* txt, size_t len) {
	    utf8proc_int32_t ch;
	    if(utf8proc_iterate((const utf8proc_uint8_t*)txt, len, &ch) != len)
		ch = 0xFFFD;
	    return ch;
	}

	static std::string utf8normal(const char* txt, size_t len, bool ucase=false) {
	    static size_t utf32_buflen = 256;
	    static utf8proc_int32_t* utf32 = new utf8proc_int32_t[utf32_buflen];

	    utf8proc_ssize_t dlen;
	    while((dlen = utf8proc_decompose((utf8proc_uint8_t*)txt, len, utf32, utf32_buflen,
			utf8proc_option_t(UTF8PROC_COMPAT|UTF8PROC_IGNORE|UTF8PROC_DECOMPOSE))) > utf32_buflen) {
		delete[] utf32;
		utf32 = new utf8proc_int32_t[utf32_buflen = (dlen+255)&~255];
	    }
	    if(dlen < 1) 
		throw "Not supposed to happen: identifier has no codepoints?";
	    if(ucase)
		for(int i=0; i<dlen; i++)
		    utf32[i] = utf8proc_toupper(utf32[i]);
	    size_t blen = 0;
	    char buf[dlen*4+1];
	    for(int i=0; i<dlen; i++)
		blen += utf8proc_encode_char(utf32[i], (utf8proc_uint8_t*)buf+blen);
	    buf[blen++] = 0;
	    return buf;
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

<*>[dD][0-7](?={NOID}|\R)		VToken(DREG, Register, text()[1] - 48);
<*>[aA][0-7](?={NOID}|\R)		VToken(AREG, Register, text()[1] - 40);
<*>[sS][pP](?={NOID}|\R)		VToken(AREG, Register, 7);
<*>[pP][cC][rR]?(?={NOID}|\R)		Token(PCR);
<*>[cC][cC][rR]?(?={NOID}|\R)		Token(CCR);
<*>[iI][rR](?={NOID}|\R)		Token(IR);
<*>[sS][mM][rR]?(?={NOID}|\R)		Token(SMR);

^{ISTART}{ID}*				start(MNEM); VToken(LABEL, String, utf8normal(text(), size()));
^\.[0-9]+				start(MNEM); VToken(LABEL, String, str());
^{SPACE}+				start(MNEM);

<*>{SPACE}*\R				start(INITIAL); Token(EOL);
<*>{SPACE}+				/* nothing */

<MNEM>{ISTART}{ID}*			start(INITIAL); VToken(OP, String, utf8normal(text(), size(), true));

{ISTART}{ID}*				VToken(IDENT, String, utf8normal(text(), size()));
\.[0-9]+[bBfF]				VToken(IDENT, String, str());
\.[bB]					VToken(SIZE, Size, 1);
\.[wW]					VToken(SIZE, Size, 2);
\.[lL]					VToken(SIZE, Size, 4);
\.[aA]					VToken(SIZE, Size, 6);
\.[qQ]					VToken(SIZE, Size, 8);
\.					VToken(IDENT, String, str());

<<					Token(LSHIFT);
>>					Token(RSHIFT);

\$[0-9a-fA-F]+				VToken(LITERAL, Value, std::strtol(text()+1, 0, 16));
[0-9]+					VToken(LITERAL, Value, std::strtol(text(), 0, 10));
@[0-7]+					VToken(LITERAL, Value, std::strtol(text()+1, 0, 8));
%[01]+					VToken(LITERAL, Value, std::strtol(text()+1, 0, 2));
'\p{Unicode}				VToken(LITERAL, Value, deutf8(text()+1, size()-1));

<STRING>[^"\\\n]+			tmpstr += str();
<STRING>\\\p{Unicode}			tmpstr += text()[0];
<STRING>["\n]				pop_state(); yylloc.begin.column=tmpstr_col; VToken(STRING, StringLit, tmpstr);
<INITIAL>\"[^"\\\n]+			push_state(STRING); tmpstr_col=yylloc.begin.column; tmpstr = str().substr(1);

<INITIAL,MNEM>\p{ASCII}			return text()[0];
<INITIAL,MNEM>\p{Non_ASCII_Unicode}+	Token(BAD);
<*>.					Token(BAD);
<*><<EOF>>				Token(EOF);

%%

