* "hard"coded bootstrap for the Nightmare vm

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

		org	6 * (8+16+ 16)	; skip over all trap vectors

kernel_stack	ds	256 * 6

reset_vec	lea	kernel_stack,a7
