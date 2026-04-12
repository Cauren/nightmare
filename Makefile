CCOPT := -g3 -O0 -std=c++20 -fdiagnostics-color=always

nightmare:			cpu.o
				g++ ${CCOPT} -o nightmare cpu.o

cpu.o:				cpu.cc cpu.hh
				g++ ${CCOPT} -c cpu.cc

