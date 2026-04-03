

nas:				nas.o nasgr.o naslex.o
				g++ -g3 -O0 -std=c++20 -o nas nas.o nasgr.o naslex.o -lreflex

nasgr.cc nasgr.hh:		nasgr.y
				bison nasgr.y

naslex.cc naslex.hh:		naslex.l
				reflex naslex.l

naslex.o:			naslex.cc nasgr.hh nas.hh
				g++ -g3 -std=c++20 -c naslex.cc

nasgr.o:			nasgr.cc naslex.hh nas.hh
				g++ -g3 -std=c++20 -c nasgr.cc

nas.o:				nas.cc naslex.hh nasgr.hh nas.hh
				g++ -g3 -O0 -std=c++20 -c nas.cc

clean:
				rm *.o nasgr.cc nasgr.hh nasgr.output naslex.cc naslex.hh nas
