#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "sha256.h"

#define BLOCKSIZE (16ull * 1024ull)

struct onefile {
	char result[HASH_SIZE];
	
	char const * path;
	size_t mapsize;
	char * filedata;

	char * level1results;
};

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

	for (int i = 1; i < argc; ++i) {
		struct onefile file;
		struct stat stat;
		file.path = argv[i];

		int fd = open(file.path, O_RDONLY);
		if (fd == -1) {
			fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		int res = fstat(fd, &stat);
		if (res == -1) {
			fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
			close(fd);
			continue;
		}

		file.mapsize = stat.st_size;

		file.filedata = (char *)mmap(NULL, file.mapsize, PROT_READ, MAP_PRIVATE, fd, 0);
		close(fd);
		
		if (file.filedata == MAP_FAILED) {
			fprintf(stderr, "While processing %s: %s\n", argv[i], strerror(errno));
			continue;
		}

		size_t chunks = file.mapsize / BLOCKSIZE;
		if (file.mapsize % BLOCKSIZE) chunks += 1;

		file.level1results = malloc(chunks * HASH_SIZE);

		char * currentresult = file.level1results;
		char * dataptr = file.filedata;
		size_t remain = file.mapsize;
		while (remain > BLOCKSIZE) {
			sha256_hash_block(dataptr, BLOCKSIZE, currentresult);
			dataptr += BLOCKSIZE;
			currentresult += HASH_SIZE;
			remain -= BLOCKSIZE;
		}
		sha256_hash_block(dataptr, remain, currentresult);

		sha256_hash_block(file.level1results, chunks * HASH_SIZE, file.result);

		free(file.level1results);
		munmap(file.filedata, file.mapsize);

		for(int i = 0; i < 32; ++i) printf("%.2hhx", file.result[i]);
		printf("\n");
	}

	return 0;
}
