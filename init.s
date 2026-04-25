* "init.x" is just a really (really) dumb shell

_STACK		seg	0

_DATA		seg	1,@004
		org	0
prompt		db	"# ",0
crlf		db	13,10,0

_BSS		seg	2,@006
		org	0
ibuf		ds	80
pad		ds	80

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
.1		rts

puts		mov	(a0)+.b,d1
		beq	.1b
		mov	#4,d0
		trap	#15
		bra	puts

eol		lea	crlf,a0
		bra	puts

_ini		lea	prompt,a0
		bsr	puts
		bsr	readln
		bsr	eol
		trap	#14

		lea	ibuf,a2
		lea	pad,a1
.1		mov	(a2)+.b,d0
		beq	_ini
		cmp	#$20,d0
		beq	.1b
.2		mov	d0,(a1)+.b
		mov	(a2)+.b,d0
		beq	.1f
		cmp	#$20,d0
		bne	.2b
.1		mov	#'.,d0
		mov	d0,(a1)+.b
		mov	#'x,d0
		mov	d0,(a1)+.b
		clr	(a1)+.b

		; we'd be parsing args here

		mov	#6,d0
		trap	#15		    ; vfork()
		tst	d0
		beq	.1f
		stop

.1		lea	pad,a0
		mov	#2,d0
		trap	#15		    ; exec()
		stop
