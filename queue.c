#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>

#include "queue.h"

void queue_init (queue_t *queue, size_t capacity)
{
	queue->items = malloc(capacity * sizeof(void*));
	queue->capacity = capacity;
	queue->size = 0;

	queue->head = queue->tail = 0;
	queue->closed = 0;

	pthread_mutex_init(&queue->mutex, NULL);
	sem_init(&queue->consumable, 0, 0);
	sem_init(&queue->produceable, 0, capacity);
}

void queue_push (queue_t *queue, void *item)
{
	/* ensure this space for product */
	sem_wait(&queue->produceable);
	/* return space if queue closed */
	if (queue->closed) {
		sem_post(&queue->produceable);
		return;
	}
	/* lock modify mutex */
	pthread_mutex_lock(&queue->mutex);

	/* perform insertion */
	queue->items[queue->head] = item;
	queue->head = (queue->head + 1) % queue->capacity;

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

	/* free up space for product */
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
