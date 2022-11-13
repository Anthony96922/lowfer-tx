CC = gcc
CFLAGS = -Wall -O2 -pedantic

OBJ = am.o lwtx.o ssb.o

lwtx: $(OBJ)
	$(CC) $(OBJ) -o lwtx -lm -lao -lsndfile -lsamplerate

lwtx.o: lwtx.h am.h

am.o: lwtx.h am.h

clean:
	rm -f *.o
