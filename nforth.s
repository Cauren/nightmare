* General model:
*
* a4 is just the data area to make (a4,dn) easier to handle
* a5 is IP
* a6 is USP
* a7 is RSP

!define word
seg	_DATA
.1		dl	.1b
		db	?2,0
?1		da	?3
!end
!define asm
!word ?1,?2,do?1
		seg	_TEXT
do?1 !end
!define colon
!word ?1,?2,dopcol
!end
!define var
!word ?1,?2,dopcreate
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
.1:
base		dl	10

		!asm	emit,"emit"
		mov	-(a6),d1
		mov	#4,d0
		trap	#15
		bra	next

		!colon	cr,"cr"
		dl	plit,13,emit
		dl	plit,10,emit
		dl	psemi

		!colon	abort,"abort"
		dl	quit

		!colon	quit,"quit"
*		dl	lbrak
.2		dl	cr,query,interp,state,at,zequ,zbran
		dl	.3f-.
		dl	pdotq
		db	" Ok",0
.3		dl	bran
		dl	.2b-.


		!asm	pcreate,"(create)"
		sta	a0,d0
		mov	d0,(a6)+
		bra	next

		!asm	pcol,"(:)"
		sta	a5,d1
		mov	d1,(a7)+
		lea	(a0),a5
next		mov	(a5)+,d0
		lea	(a4,d0),a0
		jmp	(a0)+

		!asm	key,"key"
		mov	#5,d0
		trap	#15
		mov	d0,(a6)+
		bra	next

		!asm	psemi,"(;)"
		mov	-(a7),d0
		lda	d0,a5
		bra	next

		!asm	plit,"(lit)"
		mov	(a5)+,d0
		mov	d0,(a6)+
		bra	next

		!word	warm,"warm",dowarm
		!asm	cold,"cold"
		lea	_DATA:0,a4
dowarm		lea	_STACK:0,a7
		lea	_BSS:0,a6
		mov	#10,d0
		mov	d0,base
		lea	abort+6,a5
		trap	#14
		bra	next

