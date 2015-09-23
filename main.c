#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/sysinfo.h>

#include <stdatomic.h>
#include <pthread.h>

#include "sha256.h"
#include "queue.h"
#include "tools.h"

#define BLOCKSIZE (16 * 1024)
#define QUEUE_SIZE (16 * 1024)

/* task types */
typedef enum { FILE_TASK, HASH_TASK } task_type;

/* file task */
typedef enum { STARTED, POSTED, L1DONE } state_t;


typedef struct {
	task_type type;
	
	char * path;
	uint64_t size;

	char * l1hashes;
	size_t l1hashes_size;

	/* size of max acceptable file is limited
	 * by the l1hashes field; we would have to
	 * add another level if we wanted to support
	 * huge files on 32bit archs. hence size_t
	 * is appropriate for counting numbers of work
	 * units as well */
	size_t work_posted;
	size_t work_completed;

	state_t state;
	char const * error;

	char result[HASH_SIZE];
} file_t;


/* hash task */
typedef struct {
	task_type type;

	char* result;
	char* data;
	size_t length;
	file_t * file;
} hash_t;

/* completion task */
typedef union {
	task_type type;
	file_t file;
	hash_t hash;
} completion_t;


/* queues */

queue_t file_queue;
queue_t hash_queue;
queue_t completed_queue;

_Atomic int files_done = ATOMIC_VAR_INIT(0);


/* worker threads */

void * file_worker (void * unused)
{
	struct stat stat;
	char * data = NULL;

	for (;;) {
		int err_flag = 1;
		int work_posted = 0;

		file_t * file = queue_pop(&file_queue);
		if (file == NULL) return NULL;
		
		/* simplistic read()ing */
		int fd = open(file->path, O_RDONLY);
		if (fd == -1) goto end;

		int res = fstat(fd, &stat);
		if (res == -1) goto end;

		file->size = stat.st_size;
		uint64_t chunks = file->size / BLOCKSIZE;
		assert(chunks <= SIZE_MAX);
		if (file->size % BLOCKSIZE) chunks += 1;

		file->l1hashes = malloc(chunks * HASH_SIZE);
		if (file->l1hashes == NULL) goto end;
		file->l1hashes_size = chunks * HASH_SIZE;

		char * resultptr = file->l1hashes;
		for (;;) {
			data = malloc(BLOCKSIZE);
			if (data == NULL) goto end;
			ssize_t bytes_read = read(fd, data, BLOCKSIZE);
			/* todo handle errors correctly, take care of EINTR */
			if (bytes_read == -1) goto end;

			hash_t * hash = malloc(sizeof(hash_t));
			if (hash == NULL) goto end;

			hash->type = HASH_TASK;
			hash->file = file;

			hash->result = resultptr;
			hash->data = data;
			hash->length = bytes_read;

			queue_push(&hash_queue, hash);
			work_posted += 1;
			data = NULL;

			/* on eof, break */
			if (bytes_read < BLOCKSIZE) break;
		}

		err_flag = 0;
	end:
		close(fd);
		if (err_flag) {
			free(data);
			file->error = strerror(errno);
		}
		file->work_posted = work_posted;
		queue_push(&completed_queue, file);
	}
}

void * hash_worker (void * unused)
{
	for (;;) {
		hash_t * hash = queue_pop(&hash_queue);
		if (hash == NULL) return NULL;

		sha256_hash_block(hash->data, hash->length, hash->result);
		queue_push(&completed_queue, hash);
	}
}

void do_complete_file_l1 (file_t*);
void do_complete_file_l2 (file_t*);

void * completion_worker (void * unused)
{
	for (;;) {
		completion_t * task = queue_pop(&completed_queue);
		if (task == NULL) return NULL;

		if (task->type == HASH_TASK) {
			hash_t * hash = &task->hash;
			file_t * file = hash->file;

			if (file->state != L1DONE) free(hash->data);
				/* because at L1DONE it points to l1hashes which is freed later */
			free(hash);
			
			file->work_completed += 1;
			switch (file->state) {
				case POSTED:
					if (file->work_completed == file->work_posted)
						do_complete_file_l1(file);
					break;
				case L1DONE:
					do_complete_file_l2(file);
					break;
				case STARTED:
					/* nothing */
					break;
			}

		} else if (task->type == FILE_TASK) {
			file_t * file = &task->file;
			if (file->state != STARTED) {
				fprintf(stderr, "While processing %s: invalid state of file in queue\n", file->path);
				/* not sure what to do now, just pretend that nothing happened i guess */
				continue;
			}

			file->state = POSTED;
			if (file->work_completed == file->work_posted)
				do_complete_file_l1(file);
		}
	}
}

void file_dealloc (file_t * file)
{
	free(file->l1hashes);
	free(file->path);
	free(file);

	files_done += 1;
}

void do_complete_file_l1 (file_t * file)
{
	if (file->error) {
		fprintf(stderr, "Error processing %s: %s\n", file->path, file->error);
		file_dealloc(file);
	} else {
		file->state = L1DONE;

		hash_t * hash = malloc(sizeof(hash_t));
		if (hash == NULL) {
			fprintf(stderr, "Out of memory when processing %s\n", file->path);
			file_dealloc(file);
			return;
		}

		hash->type = HASH_TASK;
		hash->file = file;
		hash->data = file->l1hashes;
		hash->length = file->l1hashes_size;
		hash->result = file->result;
		queue_push(&hash_queue, hash);
	}
}

void do_complete_file_l2 (file_t * file)
{
	/* print hash */
	for (int i = 0; i < HASH_SIZE; ++i)
		printf("%.2hhx", file->result[i]);
	printf("  %s\n", file->path);
	file_dealloc(file);
}

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

	int files_posted = 0;
	int hash_threadnum = get_nprocs();
	int file_threadnum = 16;

	/* initialize queues */
	queue_init(&file_queue, QUEUE_SIZE);
	queue_init(&hash_queue, QUEUE_SIZE);
	queue_init(&completed_queue, 256);

	/* initialize workers */
	pthread_t * file_threads = xmalloc(sizeof(pthread_t) * file_threadnum);
	for (int i = 0; i < file_threadnum; ++i)
		pthread_create(&file_threads[i], NULL, file_worker, NULL);

	pthread_t * hash_threads = xmalloc(sizeof(pthread_t) * hash_threadnum);
	for (int i = 0; i < hash_threadnum; ++i)
		pthread_create(&hash_threads[i], NULL, hash_worker, NULL);

	pthread_t completion_thread;
	pthread_create(&completion_thread, NULL, completion_worker, NULL);

	for (int i = 1; i < argc; ++i) {
		file_t * file = xmalloc(sizeof(file_t));
		file->type = FILE_TASK;
		int len = strlen(argv[i]) + 1;
		file->path = xmalloc(len);
		strncpyz(file->path, argv[i], len);
		file->state = STARTED;
		queue_push(&file_queue, file);
		files_posted += 1;
	}

	/* busy-wait for files finished because why not */
	struct timespec sleep100ms = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
	while (files_posted != files_done) {
		nanosleep(&sleep100ms, NULL);
	}

	/* stop queues */
	queue_stop(&file_queue);
	queue_stop(&hash_queue);
	queue_stop(&completed_queue);

	for (int i = 0; i < file_threadnum; ++i)
		pthread_join(file_threads[i], NULL);
	for (int i = 0; i < hash_threadnum; ++i)
		pthread_join(hash_threads[i], NULL);
	pthread_join(completion_thread, NULL);

	free(file_threads);
	free(hash_threads);

	queue_free(&file_queue);
	queue_free(&hash_queue);
	queue_free(&completed_queue);

	return 0;
}
