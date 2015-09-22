#ifndef __QUEUE_H__
#define __QUEUE_H__

#include <pthread.h>
#include <semaphore.h>

typedef struct queue {
	void** items;
	size_t capacity;
	size_t size;
	size_t head, tail;

	_Atomic int closed;
	sem_t consumable;
	sem_t produceable;
	pthread_mutex_t mutex;
} queue_t;

void queue_init (queue_t *queue, size_t capacity);
void queue_push (queue_t *queue, void *item);
void* queue_pop (queue_t *queue);
void queue_stop (queue_t *queue);
void queue_free (queue_t *queue);

#endif
