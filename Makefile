OPTFLAGS = -O2
CFLAGS = -Wall -pedantic --std=c11 $(OPTFLAGS)
LDFLAGS = -pthread
OBJS = main.o sha256.o queue.o


fastsum: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

sha256.c: sha256.h

main.c: sha256.h queue.h

queue.c: queue.h

%.o: %.c
	$(CC) $(CFLAGS) -c $<

all: fastsum

clean:
	rm -f $(OBJS)
	rm -f fastsum
