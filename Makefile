CCOPT := -g3 -O0 -std=c++20 -fdiagnostics-color=always

nightmare:	cpu.o
		g++ ${CCOPT} -o $@ $^

%.o:		%.cc
		g++ ${CCOPT} -c $<

cpu.o:		cpu.hh

%.x:		%.s
		./nas/nas -o $@ $<

nas/nas:
		$(MAKE) -C nas nas

.PHONY:		nas/nas

