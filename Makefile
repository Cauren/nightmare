CCOPT := -g3 -O0 -std=c++20 -fdiagnostics-color=always

nightmare:	cpu.o object.o os.o
		g++ ${CCOPT} -o $@ $^ -lncursesw

%.o:		%.cc
		g++ ${CCOPT} -c $<

cpu.o:		cpu.hh object.hh
object.o:	object.hh
os.o:		cpu.hh

%.x:		%.s
		./nas/nas -g -o $@ $<

nas/nas:
		$(MAKE) -C nas nas

.PHONY:		nas/nas

