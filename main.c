#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "hash.h"

void print_usage()
{
	printf("Usage: fasthash [FILE]...");
}

int main (int argc, char **argv)
{
	if (argc < 1) {
		print_usage();
		exit(0);
	}

	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1) {
			fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		struct hash hash;
		char buffer[HASH_BLOCKSIZE];
		hash_init(&hash);

		for (;;) {
			ssize_t bytes_read = read(fd, buffer, HASH_BLOCKSIZE);
			if (bytes_read == -1) {
				fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
				close(fd);
				goto continue_outerloop;
			} else if (bytes_read == 0) {
				/* eof */
				break;
			} else {
				hash_extend(&hash, buffer, bytes_read);
			}
		}

		hash_finalize(&hash);
		printf("%s  %s\n", hash.hexrepr, argv[i]);
	continue_outerloop:
		;
	}

	return 0;
}
