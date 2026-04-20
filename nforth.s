* General model:
*
* a4 is just the data area to make (a4,dn) easier to handle
* a5 is IP
* a6 is USP
* a7 is RSP
* d7 is top of stack

!define word
seg	_DATA
.1		dl	.1b
		db	?4+0
		db	#?2
?1		da	?3
!end
!define asm
!word ?1,?2,do?1,?3
		seg	_TEXT
do?1 !end
!define colon
!word ?1,?2,dopcol,?3
!end
!define var
!word ?1,?2,dopcreate,?3
?1_val !end
!end
!define br
dl  branch
		dl	?1-.-4
!end
!define	zbr
dl  zbranch
		dl	?1-.-4
!end

_STACK		seg	0

_DATA		seg	1
		org	0

_BSS		seg	2
		org	0
ustack		ds	4*1024

_TEXT		seg	3
		org	0
		bra	>docold

		seg	_DATA
.1:		ds	2

		!var	base,"base"
		dl	10
		!var	state,"state"
		dl	0
		!var	context,"context"
		dl	0
		!var	current,"current"
		dl	0
		!var	latest,"latest"
		dl	0
		!var	here,"here"
		dl	0
		!var	in,"in"
		dl	0
		!var	tib,"tib"
		ds	192
		!var	pad,"pad"
		ds	64
		!var	hld,"hld"
		dl	0

		!asm	fetch,"@"
		lea	(a4,d7),a0
		mov	(a0),d7
		bra	next

		!asm	cfetch,"c@"
		lea	(a4,d7),a0
		mov	(a0).b,d7
		bra	next

		!asm	store,"!"
		lea	(a4,d7),a0
		mov	-(a6),d0
		mov	-(a6),d7
		mov	d0,(a0)
		bra	next

		!asm	pstore,"+!"
		lea	(a4,d7),a0
		mov	-(a6),d0
		mov	-(a6),d7
		add	d0,(a0)
		bra	next

		!asm	cstore,"c!"
		lea	(a4,d7),a0
		mov	-(a6),d0
		mov	-(a6),d7
		mov	d0,(a0).b
		bra	next

		!asm	plusstore,"+!"
		lea	(a4,d7),a0
		mov	-(a6),d0
		mov	-(a6),d7
		add	d0,(a0)
		bra	next

		!asm	dup,"dup"
		mov	d7,(a6)+
		bra	next

		!asm	qdup,"?dup"
		tst	d7
		beq	next
		mov	d7,(a6)+
		bra	next

		!asm	drop,"drop"
		mov	-(a6),d7
		bra	next

		!asm	swap,"swap"
		mov	(-4,a6),d0
		mov	d7,(-4,a6)
		mov	d0,d7
		bra	next

		!asm	over,"over"
		mov	d7,(a6)+
		mov	(-8,a6),d7
		bra	next

		!asm	rot,"rot"
		mov	(-8,a6),d0
		mov	(-4,a6),d1
		mov	d1,(-8,a6)
		mov	d7,(-4,a6)
		mov	d0,d7
		bra	next

		!asm	rfrom,"r>"
		mov	d7,(a6)+
		mov	-(a7),d7
		bra	next

		!asm	tor,">r"
		mov	d7,(a7)+
		mov	-(a6),d7
		bra	next

		!asm	r,"r"
		mov	d7,(a6)+
		mov	(-4,a7),d7
		bra	next

		!asm	plus,"+"
		add	-(a6),d7
		bra	next

		!asm	minus,"-"
		neg	d7
		add	-(a6),d7
		bra	next

		!asm	and,"and"
		and	-(a6),d7
		bra	next

		!asm	or,"or"
		or	-(a6),d7
		bra	next

		!asm	xor,"xor"
		xor	-(a6),d7
		bra	next

		!asm	equal,"equal"
		sub	-(a6),d7
		beq	true_
		clr	d7
		bra	next
true_		mov	#-1,d7
		bra	next

		!asm	zless,"0<"
		tst	d7
		bmi	true_
		clr	d7
		bra	next

		!asm	zequal,"0="
		tst	d7
		beq	true_
		clr	d7
		bra	next

		!colon	query,"query"
		dl	tib,plit,132,expect,zero,in,store
		dl	psemi

		!asm	pcreate,"(create)"
		mov	d7,(a6)+
		sta	a0,d7
		bra	next

		!asm	pvocab,"(vocab)"
		sta	a0,d0
		mov	d0,a4:context_val
		bra	next

		!asm	break,"break"
		trap	#14
		bra	next

		!asm	rbrak,"]"
		mov	#1,d0
		mov	d0,a4:state_val
		bra	next

		!asm	lbrak,"[",1
		clr	a4:state_val
		bra	next

		!asm	cfa,"cfa"
		mov	(a4,d7,5).b,d0
		add	#6,d0
		btst	#0,d0
		beq	.2f
		inc	d0
.2		add	d0,d7
		bra	next

		!asm	pfa,"pfa"
		mov	(a4,d7,7).b,d0
		add	#12,d0
		btst	#0,d0
		beq	.2f
		inc	d0
.2		add	d0,d7
		bra	next

		!asm	pcol,"(:)"
		sta	a5,d1
		mov	d1,(a7)+
		lea	(a0),a5
next		mov	(a5)+,d0
.2		lea	(a4,d0,6),a0
		jmp	([-6,a0])

		!asm	execute,"execute"
		mov	d7,d0
		mov	-(a6),d7
		bra	.2b

		!asm	zero,"0"
		mov	d7,(a6)+
		clr	d7
		bra	next

		!asm	one,"1"
		mov	d7,(a6)+
		mov	#1,d7
		bra	next

		!asm	minusone,"-1"
		mov	d7,(a6)+
		mov	#-1,d7
		bra	next

		!asm	oneplus,"1+"
		inc	d7
		bra	next

		!asm	oneminus,"1-"
		dec	d7
		bra	next

		!asm	comma,","
		mov	a4:here_val,d0
		mov	d7,(a4,d0)
		add	#4,d0
		mov	d0,a4:here_val
		mov	-(sp),d7
		bra	next

		!asm	emit,"emit"
		mov	d7,d1
		mov	-(a6),d7
		mov	#4,d0
		trap	#15
		bra	next

		!colon	cr,"cr"
		dl	plit,13,emit
		dl	plit,10,emit
		dl	psemi

		!colon	space,"space"
		dl	plit,32,emit
		dl	psemi

		!asm	definitions,"definitions"
		mov	a4:context_val,d0
		mov	d0,a4:current_val
		bra	next

		!word	forth,"forth",dopvocab,1
		dl	orig

		!asm	key,"key"
		mov	#5,d0
		trap	#15
		mov	d0,(a6)+
		bra	next

		!asm	psemi,";s"
		mov	-(a7),d0
		lda	d0,a5
		bra	next

		!asm	rpstore,"rp!"
		lea	_STACK:0,a7
		bra	next

		!asm	spstore,"sp!"
		lea	_BSS:0,a6
		bra	next

		!asm	branch,"branch"
		mov	(a5)+,d0
		lea	(a5,d0),a5
		bra	next

		!asm	zbranch,"0branch"
		mov	(a5)+,d0
		tst	d7
		bne	.2f
		lea	(a5,d0),a5
.2		mov	-(a6),d7
		bra	next

		!asm	plit,"(lit)"
		mov	d7,(a6)+
		mov	(a5)+,d7
		bra	next

		!colon	shash,"<#"
		dl	pad,plit,63,plus,hld,store,psemi

		!colon	hold,"hold"
		dl	minusone,hld,pstore
		dl	hld,fetch,cstore
		dl	psemi

		!colon	hash,"#"
		dl	base,fetch,slashmod
		dl	swap,plit,9,over,less
		!zbr	.2f
		dl	plit,7,plus
.2		dl	plit,48,plus,hold,psemi

		!colon	hashs,"#s"
.2		dl	hash
		dl	dup,zequal
		!zbr	.2b
		dl	psemi

		!colon	ehash,"#>"
		dl	drop
		dl	hld,fetch
		dl	pad,plit,63,plus,over,minus
		dl	swap,oneminus,swap,over,cstore
		dl	psemi

		!colon	spaces,"spaces"
		dl	zero,max,oneplus
.2		dl	qdup
		!zbr	fini
		dl	space,oneminus
		!br	.2b
		dl	psemi

		!colon	sign,"sign"
		dl	swap,zless
		!zbr	fini
		dl	plit,'-,hold
		dl	psemi

		!colon	dotr,".r"
		dl	swap,dup,abs
		dl	shash,hashs,sign,ehash
		dl	swap,over,cfetch,minus,spaces
		dl	type
		dl	psemi

		!colon	dot,"."
		dl	zero,dotr,psemi

		!asm	max,"max"
		mov	-(a6),d0
		cmp	d0,d7
		bgt	next
		mov	d0,d7
		bra	next

		!asm	abs,"abs"
		tst	d7
		bpl	next
		neg	d7
		bra	next

		!asm	less,"<"
		cmp	-(a6),d7
		bge	true_
		clr	d7
		bra	next

		!asm	slashmod,"/mod"
		mov	-(a6),d0
		divs	d7,d0
		mov	d7,(a6)+
		mov	d0,d7
		bra	next

		!asm	cold,"cold"
		lea	_DATA:0,a4
		lea	_STACK:0,a7
		lea	_BSS:0,a6
		lea	_cold,a5
		lea	fault_handler,a0
		clr	d0
		trap	#0
*		lea	break_handler,a0
*		mov	#1,d0
*		trap	#0
		bra	next

		seg	_DATA

_cold		dl	orig,plit,forth
		dl	plit,6,plus,store
		dl	plit,dpsace,here,store
		dl	abort

fault_msg	db	#"memory fault"
usp_msg		db	#"stack underrun"
rsp_msg		db	#"return stack underrun"

		seg	_TEXT
fault_handler	sta	a6,d0
		sta	a7,d1
		lea	_DATA:0,a4
		lea	_STACK:0,a7
		lea	_BSS:4,a6
		tst	d0
		bpl	.2f
		lea	a4:usp_msg,a0
		bra	.3f
.2		tst	d1
		bpl	.2f
		lea	a4:rsp_msg,a0
		bra	.3f
.2		lea	a4:fault_msg,a0
.3		sta	a0,d7
		lea	a4:error+6,a5
		bra	next

*break_handler	trap	#14
*
		!colon	abort,"abort"
		dl	spstore
		dl	plit,10,base,store
		dl	cr,pdotq
		db	#"NVM Forth 0.1"
		dl	cr
		dl	forth,definitions
		dl	quit

		!colon	error,"error"
		dl	space,pad,type
		dl	plit,'?,emit,space
		dl	type,quit

		!colon	quit,"quit"
		dl	lbrak
.2		dl	rpstore,cr
		dl	query,interpret
		dl	state,fetch,zequal
		!zbr	.2b
		dl	pdotq
		db	#" Ok"
		!br	.2b

		!asm	pdotq,"(.\")"
		mov	#4,d0
		lea	(a5),a0
		bsr	_type_a0
		sta	a0,d0
		btst	#0,d0
		beq	.2f
		inc	d0
.2		lda	d0,a5
		bra	next

		!asm	type,"type"
		lea	(a4,d7),a0
		mov	-(a6),d7
		bsr	_type_a0
		bra	next

_type_a0	mov	(a0)+.b,d2
		beq	.3f
		mov	#4,d0
.2		mov	(a0)+.b,d1
		trap	#15
		dec	d2
		bne	.2b
.3		rts

		!asm	word,"word"
		mov	a4:in_val,d3	    ; index into source
		mov	#1,d4		    ; index into destination
		lea	a4:tib_val,a1	    ; input buffer
		lea	a4:pad_val,a0	    ; pad
		bra	.3f
.2		inc	d3
.3		mov	(a1,d3).b,d0
		beq	.4f
		cmp	d0,d7
		beq	.2b
.2		cmp	#31,d4
		bge	.4f
		mov	d0,(a0,d4).b
		inc	d4
		inc	d3
		mov	(a1,d3).b,d0
		beq	.4f
		cmp	d0,d7
		bne	.2b
.4		clr	(a0,d4).b	    ; nul at the end to be safe
		dec	d4
		mov	d4,(a0).b
		mov	d3,a4:in_val
		sta	a0,d7
		bra	next

		!colon	literal,"literal",1
		dl	state,fetch
		!zbr	.2f
		dl	plit,plit,comma,comma
.2		dl	psemi

		!asm	number,"number"
		lea	(a4,d7),a1
		mov	#0,d5		    ; sign
		mov	a4:base_val,d6	    ; base
		mov	#0,d7		    ; value
		mov	(a1)+.b,d4	    ; length
		beq	.3f
		mov	(a1)+.b,d0
		cmp	#'-,d0
		bne	.2f
		mov	#1,d5		    ; negative
		dec	d4
		beq	.3f
		mov	(a1)+.b,d0
.2		sub	#'0,d0
		blt	.3f
		cmp	#10,d0
		blt	.4f
		sub	#'A-'0,d0
		blt	.3f
		cmp	#25+32,d0
		bgt	.3f
		cmp	#25,d0
		ble	.5f
		sub	#32,d0
		bmi	.3f
.5		add	#10,d0
.4		cmp	d6,d0
		bge	.3f
		mulu	d6,d7
		bvs	.3f
		add	d0,d7
		bvs	.3f
		mov	(a1)+.b,d0
		dec	d4
		bne	.2b
		tst	d5
		beq	next
		neg	d7
		bra	next
.3		lea	a4:badw_msg,a0
		sta	a0,d7
		lea	a4:error+6,a5
		bra	next

		seg	_DATA
badw_msg	db	#"not found"

		!asm	find,"find"
		mov	d7,(a6)+
		lea	a4:pad_val,a1	    ; word to match
		mov	(a1)+.b,d2	    ; length to match
		mov	a4:context_val,d0
		mov	(a4,d0),d0
		lea	(a4,d0),a2	    ; context
		bsr	find_
		bne	.2f
		mov	a4:current_val,d0
		mov	(a4,d0),d0
		lea	(a4,d0),a2
		bsr	find_
.2		mov	d0,d7
		bra	next

find_		cmp	(5,a2).b,d2
		bne	.3f
		clr	d0
.4		mov	(a2,d0,6).b,d1
		cmp	(a1,d0).b,d1
		bne	.3f
		inc	d0
		cmp	d0,d2
		bne	.4b
		sta	a2,d0
		mov	d0,(a6)+
		mov	(4,a2).b,d0
		mov	d0,(a6)+
		mov	#-1,d0
		rts
.3		mov	(a2),d0
		lea	(a4,d0),a2
		bne	find_
		clr	d0
		rts

		!colon	interpret,"interpret"
.2		dl	plit,32,word
		dl	cfetch
		!zbr	fini
		dl	find
		!zbr	.4f
		!zbr	.3f
		dl	state,fetch
		!zbr	.3f
		dl	cfa,comma
		!br	.2b
.3		dl	cfa,execute
		!br	.2b
.4		dl	pad,number,literal
		!br	.2b
fini		dl	psemi

		!asm	expect,"expect"
		mov	-(a6),d0
		lea	(a4,d0),a2
		dec	d7		    ; space for null
		mov	#0,d6		    ; index into tib
.2		mov	#5,d0
		trap	#15
		cmp	#$08,d0
		beq	ex_bs
		cmp	#$7f,d0
		beq	ex_bs
		cmp	#$0a,d0
		beq	ex_cr
		cmp	#$0d,d0
		beq	ex_cr
		cmp	#$15,d0		    ; ^U
		beq	ex_dell
		cmp	#$17,d0		    ; ^W
		beq	ex_delw
		cmp	#$20,d0
		blt	.2b
		cmp	d7,d6
		bge	.2b
		mov	d0,(a2,d6).b
		inc	d6
		mov	d0,d1
		mov	#4,d0
		trap	#15
		bra	.2b
ex_back		mov	#$08,d1
		mov	#4,d0
		trap	#15
		mov	#$20,d1
		trap	#15
		mov	#$08,d1
		trap	#15
		dec	d6
		rts
ex_bs		tst	d6
		beq	.2b
		bsr	ex_back
		bra	.2b
ex_delw		tst	d6
		beq	.2b
		bsr	ex_back
		mov	(a2,d6).b,d0
		cmp	#32,d0
		beq	.2b
		bra	ex_delw
ex_dell		tst	d6
		beq	.2b
		bsr	ex_back
		bra	ex_dell
ex_cr		clr	(a2,d6).b
		mov	-(a6),d7
		bra	next

** This must be the last definition
** in the basic word set

		seg	_DATA
_orig		equ	.
		!asm	orig,"orig"
		mov	d7,(a6)+
		lea	a4:_orig,a0
		sta	a0,d7
		bra	next


dspace		ds	64*1024		    ; 64k of fun
