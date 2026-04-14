* "hard"coded bootstrap for the Nightmare vm

_segmap		seg	@777776
_root		seg	@777777
_uarea		seg	4

num_segs	equ	32

segmap		ds	16*num_segs
u_pid		ds	2

		seg	_root
		org	0

* CPU faults vectors (8)

		da	reset_vec
		da	dfault_vec	; not used "for real" since this halts the VM
		da	fault_vec	; address out of segment / bad segment
		da	inval_vec	; invalid instruction
		da	perm_vec	; attempt to use a super segment while not super
		da	access_vec	; rwx mismatch
		da	notrap_vec
		da	notrap_vec

		org	6 * (8+16+16)	; skip over all trap vectors

context		da	0
init_fname	db	"init.x",0

kernel_stack	ds	256 * 6

reset_vec	lea	kernel_stack,a7
		mov	#4*1024,d1
		mov	#0,d0
		trap	#15		; kmalloc
		lea	context,a1
		mov	a0,(a1)
		ssma	(2,a1)
		mov	#num_segs,d0
		ssml	d0
		lsl	#4,d0
.1		clr	(a0)+
		dec	d0
		bne	.1b

		* initialize segmap for init process
