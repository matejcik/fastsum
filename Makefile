CFLAGS = -Wall -pedantic --std=c11
OBJS = main.o hash.o


fasthash: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

hash.c: hash.h

main.c: hash.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

all: fasthash

clean:
	rm -f $(OBJS)
	rm -f fasthash
