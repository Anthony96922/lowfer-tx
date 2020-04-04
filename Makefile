CC = gcc
CFLAGS = -Wall -O2 -pedantic

LWTX_OBJ = am.o lwtx.o

lwtx: $(LWTX_OBJ)
	$(CC) $(LWTX_OBJ) -o lwtx -s -lm -lao -lsndfile -lsamplerate

lwtx.o: lwtx.h am.h

am.o: lwtx.h am.h

clean:
	rm -f *.o
