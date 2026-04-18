* "hard"coded bootstrap for the Nightmare vm

_STACK		seg	0

_DATA		seg	1
		org	0
greeting	db	"Hello, world!", 10, 0

_BSS		seg	2
		org	0
var1		ds	4
var2		ds	4

_TEXT		seg	3

		org	0
_ini		lea	greeting,a0
		mov	#3,d0
		trap	#15

.1		mov	#5,d0
		trap	#15
		mov	d0,d1
		mov	#4,d0
		trap	#15
		cmp	#'q,d1
		bne	.1b

		stop
