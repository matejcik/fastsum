#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <semaphore.h>

#include "queue.h"
#include "tools.h"

void queue_init (queue_t *queue, size_t capacity)
{
	queue->items = malloc(capacity * sizeof(void*));
	queue->initial_capacity = queue->capacity = capacity;
	queue->size = 0;

	queue->head = queue->tail = 0;
	queue->closed = 0;

	queue->dynamic = 0;

	pthread_mutex_init(&queue->mutex, NULL);
	sem_init(&queue->consumable, 0, 0);
	sem_init(&queue->produceable, 0, capacity);
}

void queue_init_dynamic (queue_t *queue, size_t initial_capacity)
{
	queue_init(queue, initial_capacity);
	queue->dynamic = 1;
}

void queue_push (queue_t *queue, void *item)
{
	if (!queue->dynamic) {
		/* ensure this space for product */
		sem_wait(&queue->produceable);
		/* return space if queue closed */
		if (queue->closed) {
			sem_post(&queue->produceable);
			return;
		}
	} else if (queue->closed) {
		return;
	}

	/* lock modify mutex */
	pthread_mutex_lock(&queue->mutex);

	if (queue->dynamic && queue->size == queue->capacity) {
		assert(queue->head == queue->tail);
		/* allocate additional space */
		int newcap = queue->capacity + queue->initial_capacity;
		fprintf(stderr,"realloc to %d\n", newcap);
		void ** newitems = xmalloc(newcap * sizeof(void*));
		fprintf(stderr,"realloc to %d done\n", newcap);
		/* copy items so that queue starts at 0 */
		memcpy(newitems, queue->items + queue->tail, (queue->capacity - queue->tail) * sizeof(void*));
		memcpy(newitems + (queue->capacity - queue->tail), queue->items, queue->tail * sizeof(void*));

		queue->tail = 0;
		queue->head = queue->capacity;
		queue->capacity = newcap;

		free(queue->items);
		queue->items = newitems;
	}

	/* perform insertion */
	queue->items[queue->head] = item;
	queue->head = (queue->head + 1) % queue->capacity;

	queue->size += 1;

	/* enable consumer to enter pop-mode */
	sem_post(&queue->consumable);
	/* unlock modify mutex */
	pthread_mutex_unlock(&queue->mutex);
}

void * queue_pop (queue_t *queue)
{
	/* ensure there is product to consume */
	sem_wait(&queue->consumable);
	/* closed queue, quit */
	if (queue->closed) {
		sem_post(&queue->consumable);
		return NULL;
	}

	/* lock modify mutex */
	pthread_mutex_lock(&queue->mutex);

	/* perform removal */
	void * item = queue->items[queue->tail];
	queue->tail = (queue->tail + 1) % queue->capacity;

	queue->size -= 1;

	/* free up space for product */
	if (!queue->dynamic)
		sem_post(&queue->produceable);
	/* unlock modify mutex */
	pthread_mutex_unlock(&queue->mutex);

	return item;
}


void queue_stop (queue_t *queue)
{
	/* first set closed flag */
	queue->closed = 1;
	/* then post both semaphores */
	sem_post(&queue->consumable);
	sem_post(&queue->produceable);
	/* If a thread is in the middle of something while the queue is closed,
	 * it must have cleared the semaphores, so there is valid something
	 * for it to do. It's a responsibility of the caller to join() all threads
	 * before issuing queue_free, so that threads that managed to
	 * slip by at time of closing are certain to have finished. */
}

void queue_free (queue_t *queue)
{
	sem_destroy(&queue->consumable);
	sem_destroy(&queue->produceable);
	pthread_mutex_destroy(&queue->mutex);

	free(queue->items);
}
