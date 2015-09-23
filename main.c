#define _DEFAULT_SOURCE
/* for dirent entry types, we might not need this after all? */

#include <assert.h>
#include <dirent.h>
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

#define BIGFILE_LIMIT (1024 * 1024)

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

_Atomic int directories_enqueued = ATOMIC_VAR_INIT(0);
_Atomic int files_done = ATOMIC_VAR_INIT(0);
_Atomic int files_posted = ATOMIC_VAR_INIT(0);
pthread_mutex_t bigfile_mutex = PTHREAD_MUTEX_INITIALIZER;


/* worker threads */

void do_process_file (file_t *, off_t size);
void do_process_directory (file_t *);

void * file_worker (void * unused)
{
	struct stat st;

	for (;;) {
		file_t * file = queue_pop(&file_queue);
		if (file == NULL) return NULL;

		int res = stat(file->path, &st);
		if (res == -1) {
			file->error = strerror(errno);
			queue_push(&completed_queue, file);
			continue;
		}

		int mode = st.st_mode & S_IFMT;
		if (mode == S_IFREG)
			do_process_file(file, st.st_size);
		else if (mode == S_IFDIR)
			do_process_directory(file);
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


/* auxilliary functions */

void file_dealloc (file_t * file)
{
	free(file->l1hashes);
	free(file->path);
	free(file);

	files_done += 1;
}

void do_process_directory (file_t * dir)
{
	DIR * dirfd;
	struct dirent * dirent;
	file_t * file = NULL;
	char * newpath = NULL;

	int pathlen = strlen(dir->path);

	/* TODO lock bigfile mutex for directories as well? */
	pthread_mutex_lock(&bigfile_mutex);
	/* we don't need to hold it */
	pthread_mutex_unlock(&bigfile_mutex);

	dirfd = opendir(dir->path);
	if (dirfd == NULL) goto error;

	errno = 0;
	while ((dirent = readdir(dirfd))) {
		if (dirent->d_name[0] == '.' &&
		     (dirent->d_name[1] == 0 ||
		       (dirent->d_name[1] == '.' && dirent->d_name[2] == 0)
		     )
		   ) continue;

		file_t * file = malloc(sizeof(file_t));
		if (file == NULL) goto error;
		memset(file, 0, sizeof(file_t));

		int len = strlen(dirent->d_name);
		newpath = malloc(pathlen + 1 + len + 1);
		if (newpath == NULL) goto error;
		strncpy(newpath, dir->path, pathlen);
		newpath[pathlen] = '/';
		strncpy(newpath + pathlen + 1, dirent->d_name, len);
		newpath[pathlen + 1 + len] = 0;

		file->type = FILE_TASK;
		file->path = newpath;
		file->state = STARTED;
		if (dirent->d_type == DT_DIR) directories_enqueued += 1;
		queue_push(&file_queue, file);

		newpath = NULL;
		file = NULL;
		files_posted += 1;
	}

	directories_enqueued -= 1;

	if (!errno) {
		closedir(dirfd);
		file_dealloc(dir);
		return;
	}

error:
	dir->error = strerror(errno);
	free(file);
	free(newpath);
	closedir(dirfd);
	queue_push(&completed_queue, dir);
}

void do_process_file (file_t * file, off_t size)
{
	int err_flag = 1;
	int work_posted = 0;
	int fd = -1;
	char * data = NULL;

	/* enter "bigfile" crit section */
	pthread_mutex_lock(&bigfile_mutex);
	/* if this is not big file, leave immediately */
	if (size < BIGFILE_LIMIT) pthread_mutex_unlock(&bigfile_mutex);

	/* simplistic read()ing */
	fd = open(file->path, O_RDONLY);
	if (fd == -1) goto end;

	file->size = size;
	uint64_t chunks = file->size / BLOCKSIZE;
	assert(chunks <= SIZE_MAX);
	if (file->size % BLOCKSIZE) chunks += 1;

	file->l1hashes = malloc(chunks * HASH_SIZE);
	if (file->l1hashes == NULL) goto end;
	
	char * resultptr = file->l1hashes;
	for (;;) {
		data = malloc(BLOCKSIZE);
		if (data == NULL) goto end;
		ssize_t bytes_read = read(fd, data, BLOCKSIZE);
		/* todo handle errors correctly, take care of EINTR */
		if (bytes_read == -1) goto end;

		/* filesize is multiple of BLOCKSIZE and eof happened */
		if (!bytes_read) break;

		if (work_posted == chunks) {
			/* unexpected successful read over limit */
			/* set custom error */
			file->error = "File grew while hashing";
			goto end;
		}

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
	if (err_flag) {
		free(data);
		if (!file->error) file->error = strerror(errno);
	}
	if (size >= BIGFILE_LIMIT) pthread_mutex_unlock(&bigfile_mutex);
	close(fd);
	file->work_posted = work_posted;
	file->l1hashes_size = work_posted * HASH_SIZE;
	queue_push(&completed_queue, file);
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

	int hash_threadnum = get_nprocs();
	int file_threadnum = 1;

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
		file->path = strdup(argv[i]);

		/* strip trailing slash(es) */
		int len = strlen(file->path);
		while (file->path[len] == '/') file->path[len--] = 0;

		file->state = STARTED;
		queue_push(&file_queue, file);
		files_posted += 1;
	}

	/* busy-wait for files finished because why not */
	struct timespec sleep100ms = { .tv_sec = 0, .tv_nsec = 100 * 1000 * 1000 };
	/* now silently pray that all directories are enumerated before hashing
	 * catches up with number of processed files */
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
