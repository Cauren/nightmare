

nas:				nasparser.o nasgr.o naslex.o nasasm.o
				g++ -g3 -O0 -std=c++20 -o nas nasparser.o nasgr.o naslex.o nasasm.o -lreflex -lutf8proc

nasgr.cc nasgr.hh:		nasgr.y
				bison nasgr.y

naslex.cc naslex.hh:		naslex.l
				reflex naslex.l

naslex.o:			naslex.cc nasgr.hh nasparser.hh
				g++ -g3 -std=c++20 -c naslex.cc

nasgr.o:			nasgr.cc naslex.hh nasparser.hh
				g++ -g3 -std=c++20 -c nasgr.cc

nasparser.o:			nasparser.cc naslex.hh nasgr.hh nasparser.hh nasasm.hh
				g++ -g3 -O0 -std=c++20 -c nasparser.cc

nasasm.o:			nasasm.cc naslex.hh nasgr.hh nasparser.hh nasasm.hh
				g++ -g3 -O0 -std=c++20 -c nasasm.cc

clean:
				rm *.o nasgr.cc nasgr.hh nasgr.output naslex.cc naslex.hh nas
