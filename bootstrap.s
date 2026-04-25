* "hard"coded bootstrap for the Nightmare vm

_segmap		seg	@777776
_root		seg	@777777
_uarea		seg	4

num_segs	equ	32

		org	16*num_segs
u_pid		ds	2
u_vec_fault	ds	6
u_vec_break	ds	6

		seg	_root
		org	0

* CPU faults vectors (8)

		da	reset_vec
		da	dfault_vec	; not used "for real" since this halts the VM
		da	fault_vec	; address out of segment / bad segment
		da	inval_vec	; invalid instruction
		da	perm_vec	; attempt to use a super segment while not super
		da	access_vec	; rwx mismatch
		da	break_vec	; breakpoint
		da	notrap_vec

		da	trap0_vec

		org	6 * (8+16+16)	; skip over all trap vectors

context		da	0
		da	0		; scratch

version		db	"Nightmare bootstrap 0.1",13,10,10,0
init_fname	db	"init.x",0

kernel_stack	ds	256 * 6

dfault_vec	trap	#14
		stop

inval_vec	trap	#14
		stop

perm_vec	trap	#14
		stop

access_vec	movm	d0,a0,(a7)+
		lea	_uarea:u_vec_fault,a0
		bsr	uspace_vec
		stop

fault_vec	movm	d0,a0,(a7)+
		lea	_uarea:u_vec_fault,a0
		bsr	uspace_vec
		stop

break_vec	movm	d0,a0,(a7)+
		lea	_uarea:u_vec_break,a0
		bsr	uspace_vec
		trap	#14		; this one is legit :-)
		rte

trap0_vec	tst	d0
		beq	.1f
		sta	a0,_uarea:u_vec_break
		rte
.1		sta	a0,_uarea:u_vec_fault
		rte

reset_vec	lea	kernel_stack,a7
		mov	#4*1024,d1

		mov	#6,d0
		trap	#15		; faux-fork()

		lea	_root:context,a1
		sta	a0,(a1)
		lea	(a0),a6
		ssma	(2,a1)
		mov	#num_segs,d0
		ssml	d0

		lea	version,a2
.1		mov	(a2)+.b,d1
		beq	.2f
		mov	#4,d0
		trap	#15
		bra	.1b
.2		nop

		* load init.x userspace
		lea	init_fname,a0
		mov	#2,d0
		trap	#15

		* build stack frame
		clr	(a7)+.w		; CCR
		clr	(a7)+.w		; IR
		clr	(a7)+.w		; SMR
		clr	(a7)+.w		; (fault / none)
		clr	(a7)+
		clr	(a7)+.w		; SSP 0:0
		clr	(a7)+
		sta	a0,(a7)+
		rte

uspace_vec	tst	(a0).w
		bne	.1f
		tst	(2,a0)
		beq	.2f
.1		lda	(a0),a0
		lea	(-6,a7),a7	; unbsr
		sta	a0,(-16,a7)
		movm	-(a7),d0,a0
		rte
.2		rts

