OPTFLAGS = -O2
CFLAGS = -Wall -pedantic --std=c11 -D_POSIX_C_SOURCE=9999999999 $(OPTFLAGS)
LDFLAGS = -pthread
OBJS = main.o sha256.o queue.o tools.o


fastsum: $(OBJS)
	$(CC) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c *.h
	$(CC) $(CFLAGS) -c $<

all: fastsum

clean:
	rm -f $(OBJS)
	rm -f fastsum
