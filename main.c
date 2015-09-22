#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include <stdatomic.h>

#include "sha256.h"
#include "queue.h"

#define BLOCKSIZE (16 * 1024)
#define QUEUE_SIZE (16 * 1024)

struct onefile {
	char result[HASH_SIZE];
	
	char const * path;
	size_t mapsize;
	char * filedata;

	char * level1results;
	size_t l1resultsize;
	_Atomic size_t work_remaining;
};

struct task {
	char* result;
	char* data;
	size_t length;
	struct onefile * related_file;
	int level2;
};

queue_t queue;

void print_usage()
{
	printf("Usage: fastsum [FILE]...");
}

void finalize_file(struct onefile * file)
{
	free(file->level1results);
	munmap(file->filedata, file->mapsize);

	for(int i = 0; i < 32; ++i) printf("%.2hhx", file->result[i]);
	printf("  %s\n", file->path);
	/* no free(file) now */
}

void hash_worker(struct task * task)
{
	sha256_hash_block(task->data, task->length, task->result);

	if (!task->level2) {
		size_t work = task->related_file->work_remaining--;
		if ((work % 250) == 0) printf("%ld\n", work);
		if (work == 1) {
			/* we are the last */
			struct task * l2task = malloc(sizeof(struct task));
			l2task->level2 = 1;
			l2task->related_file = task->related_file;

			l2task->result = task->related_file->result;
			l2task->data = task->related_file->level1results;
			l2task->length = task->related_file->l1resultsize;
			
			queue_push(&queue, l2task);
		}
	} else {
		finalize_file(task->related_file);
	}
	free(task);
}

void * queue_cycler (void * unused)
{
	for (;;) {
		struct task * task = queue_pop(&queue);

		if (task == NULL) return NULL;
			/* queue closed */
		hash_worker(task);
	}
}

int main (int argc, char **argv)
{
	struct onefile file;
	if (argc < 2) {
		print_usage();
		exit(0);
	}

	queue_init(&queue, QUEUE_SIZE);

	pthread_t threads[4];
	for (int i = 0; i < 4; i++) {
		pthread_create(&threads[i], NULL, queue_cycler, NULL);
	}

	for (int i = 1; i < argc; ++i) {
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

		printf("map size %ld, %ld chunks\n", file.mapsize, chunks);
		file.l1resultsize = chunks * HASH_SIZE;
		file.work_remaining = chunks;

		file.level1results = malloc(file.l1resultsize);

		char * currentresult = file.level1results;
		char * dataptr = file.filedata;
		size_t remain = file.mapsize;
		size_t dc = 0;
		while (remain > BLOCKSIZE) {
			struct task * task = malloc(sizeof(struct task));
			task->level2 = 0;
			task->data = dataptr;
			task->length = BLOCKSIZE;
			task->result = currentresult;
			task->related_file = &file;
			queue_push(&queue, task);

			dataptr += BLOCKSIZE;
			currentresult += HASH_SIZE;
			remain -= BLOCKSIZE;
			dc++;
		}
		struct task * task = malloc(sizeof(struct task));
		printf("inserted %ld tasks\n", dc);
		task->level2 = 0;
		task->data = dataptr;
		task->length = remain;
		task->result = currentresult;
		task->related_file = &file;
		queue_push(&queue, task);
	}

	printf("work remaining now: %ld\n", file.work_remaining);
	queue_stop(&queue);
	for (int i = 0; i < 4; i++)
		pthread_join(threads[i], NULL);
	queue_free(&queue);

	return 0;
}
