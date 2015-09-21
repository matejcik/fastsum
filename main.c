#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>

#include "sha256.h"

#define BLOCKSIZE (16 * 1024)

void print_usage()
{
	printf("Usage: fastsum [FILE]...");
}

int main (int argc, char **argv)
{
	if (argc < 2) {
		print_usage();
		exit(0);
	}

	for (int i = 1; i < argc; i++) {
		int fd = open(argv[i], O_RDONLY);
		if (fd == -1) {
			fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		char buffer[BLOCKSIZE];
		char result[HASH_SIZE];

		for (;;) {
			ssize_t bytes_read = read(fd, buffer, BLOCKSIZE);
			if (bytes_read == -1) {
				fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
				close(fd);
				goto continue_outerloop;
			} else if (bytes_read == 0) {
				/* eof */
				break;
			} else {
				sha256_hash_block(buffer, bytes_read, result);
			}
		}

		for(int i = 0; i < 32; i++) printf("%.2hhx", result[i]);
		printf("\n");
	continue_outerloop:
		;
	}

	return 0;
}
