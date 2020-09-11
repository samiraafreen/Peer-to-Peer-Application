CC=gcc
#CFLAGS=-g -I. -fsanitize=address -fno-omit-frame-pointer -pedantic-errors -Wall -Wextra -gline-tables-only
CFLAGS=-g -I.
LDFLAGS=-lpthread
DEPS = mrt.h
OBJ1 = peer.o mrt.o

%.o: %.c $(DEPS)
	$(CC) -c -o $@ $< $(CFLAGS) $(LDFLAGS)

all: peer

peer: $(OBJ1)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

receiver: $(OBJ2)
	$(CC) -o $@ $^ $(CFLAGS) $(LDFLAGS)

generator: $(OBJ3)
	$(CC) -o $@ $^ $(CFLAGS)


.PHONY: clean

clean:
	rm -f *.o *~ core sender receiver mrt
