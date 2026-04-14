* "hard"coded bootstrap for the Nightmare vm

_segmap		seg	@777776
_root		seg	@777777

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

context		dl	0
init_fname	db	"init.x",0

kernel_stack	ds	256 * 6

reset_vec	lea	kernel_stack,a7
		mov	#4*1024,d1
		mov	#0,d0
		trap	#15
		mov	d0,context
		ssma	d0
		ssml	#16.w
		lea	_segmap:0,a6

		lea	init_fname,a0
		mov	#1,d0
		trap	#15
