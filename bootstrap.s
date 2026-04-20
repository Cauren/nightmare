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

version		db	27,'H,27,'J
		db	"Nightmare bootstrap 0.1",13,10,10,0
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
		mov	#0,d0
		trap	#15		; kmalloc
		lea	context,a1
		sta	a0,(a1)
		lea	(a0),a6
		ssma	(2,a1)
		mov	#num_segs,d0
		ssml	d0
		lsl	#2,d0
.1		clr	(a0)+
		dec	d0
		bne	.1b

		* initialize segmap for init process
		* at this point a6 points to the segmap

		* SEG 4: uarea
		lea	(16*4,a6),a0   ; seg 4
		mov	(2,a1),d0
		mov	d0,(a0)
		mov	#4*1024,d0
		mov	d0,(4,a0)
		mov	#@16,d0		; srw-
		mov	d0,(8,a0).w

		* SEG 0: ustack
		mov	#8*1024,d1
		mov	#0,d0
		trap	#15
		sta	a0,d0
		mov	d0,(a6)
		mov	d1,(4,a6)
		mov	#@06,d0		; -rw-
		mov	d0,(8,a6).w

		lea	version,a0
		mov	#3,d0
		trap	#15

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
		mov	#3,d0		; PC 3:0
		mov	d0,(a7)+.w
		clr	(a7)+
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

