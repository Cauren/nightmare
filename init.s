* "init.x" is just a really (really) dumb shell

_STACK		seg	0

_DATA		seg	1,@004
		org	0
prompt		db	"# ", 0

_BSS		seg	2,@006
		org	0
ibuf		ds	80

_TEXT		seg	3,@001
		start	_ini

		org	0

readln		lea	ibuf,a2
		mov	#78,d7
		mov	#0,d6		    ; index into ibuf
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
		rts


