main:  main.c libmy.a
	cc -o main main.c  -lmy  -L.

libmy.a: libpgm.c
	cc -c libpgm.c
	ar rvf libmy.a libpgm.o

clean:
	rm -f main libmy.a *.o
