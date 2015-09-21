CFLAGS = -Wall -pedantic --std=c11 -O0 -g
OBJS = main.o sha256.o


fastsum: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

sha256.c: sha256.h

main.c: sha256.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

all: fastsum

clean:
	rm -f $(OBJS)
	rm -f fastsum
