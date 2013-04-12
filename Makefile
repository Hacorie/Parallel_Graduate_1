all: st1 findcopy

st1: st1-3.cpp libpp.a
	mpic++ -o st1-3 st1-3.cpp -lpp -L.

findcopy: findcopy.cpp libpp.a
	mpic++ -o findcopy findcopy.cpp -lpp -L.

libpp.a: pp.cpp
	mpic++ -c pp.cpp
	ar rvf libpp.a pp.o

clean:
	rm -f st1-3 findcopy test*.txt findcopy.txt libpp.a *.o
