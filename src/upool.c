/*  uPool - A minimal POSIX thread pool.
 *
 *  Copyright (C) 2017  Tasos Bakogiannis.
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 3 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>

#include "upool.h"


#define up_handle_error_en(msg, en, retv) \
    do { errno = en; perror(msg); return retv; } while (0)

#define up_handle_error(msg, retv) \
    do { perror(msg); return retv; } while (0)


/* A task to be executed. */
typedef struct up_task {
    void (*task_routine) (void *);        /* Pointer to the routine to execute. */
    void *arg;                            /* Pointer to the arg of the routine. */
} up_task_t;

/* A node of the task queue (linked list). */
typedef struct up_node {
    up_task_t task;                       /* The task to be executed. */
    struct up_node *next;                 /* Pointer to the next queue node. */
} up_node_t;

/* The thread pool. */
struct up_pool {
    size_t thread_count;                  /* Number of threads of the Pool. */
    size_t enq_count, deq_count;          /* Enqueued/Dequeued task counters. */
    pthread_t *threads;                   /* Array of thread IDs. */
    pthread_cond_t cond;                  /* Condition to signal threads for tasks. */
    pthread_mutex_t enq_lock, deq_lock;   /* Task queue's locks. */
    up_node_t *head, *tail;               /* Task queue's head, tail. */
};


/* Enqueue a new task into the pool's queue.
 *
 * A new `up_node_t` is allocated, the `task` is copied into the
 * new `node` and then the `node` is attached at the `pool->tail`
 * of the task queue. Finally, the `pool->cond` is signaled so
 * that one consumer thread waiting on `pool->cond` can wake up
 * and consume the new task.
 */
static int up_pool_enq(up_pool_t *pool, up_task_t *task)
{
    int retv;
    up_node_t *node;

    node = (up_node_t *) malloc(sizeof(up_node_t));
    if (node == NULL) {
        up_handle_error("up_pool_enq:malloc", UP_ERROR_MALLOC);
    }

    memcpy((void *) &node->task, (const void *) task, sizeof(up_task_t));
    node->next = NULL;

    retv = pthread_mutex_lock(&pool->enq_lock);
    if (retv != 0) {
        free(node);
        up_handle_error_en("up_pool_enq:pthread_mutex_lock", retv, UP_ERROR_MUTEX_LOCK);
    }

    pool->tail->next = node;
    pool->tail = node;

    pool->enq_count += 1;

    retv = pthread_mutex_unlock(&pool->enq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_enq:pthread_mutex_unlock", UP_ERROR_MUTEX_LOCK);
    }

    pthread_cond_signal(&pool->cond);

    return UP_SUCCESS;
}

/* Dequeue a task from the pool's queue.
 *
 * The dequeued `pool->head->task` is copied to `task` and then the
 * `pool->head` node is freed.
 */
static int up_pool_deq(up_pool_t *pool, up_task_t *task)
{
    int retv;
    up_node_t *old_head;

    retv = pthread_mutex_lock(&pool->deq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_deq:pthread_mutex_lock", UP_ERROR_MUTEX_LOCK);
    }

    pool->deq_count += 1;

    while (pool->head->next == NULL) {
        /* If the queue is empty block until a producer thread enqueues
         * a task and signals the condition.
         *
         * This is the only Cancellation Point of a consumer thread.
         * From here it's safe to jump to `up_pool_worker_cleanup`. */
        pthread_cond_wait(&pool->cond, &pool->deq_lock);
    }

    old_head = pool->head;

    pool->head = pool->head->next;

    memcpy((void *) task, (const void *) &pool->head->task, sizeof(up_task_t));

    memset((void *) &pool->head->task, 0, sizeof(up_task_t));

    retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_deq:pthread_mutex_unlock", UP_ERROR_MUTEX_LOCK);
    }

    free(old_head);

    return UP_SUCCESS;
}

/* Do thread cleanup on cancellation.
 *
 * Since a consumer thread is cancellable only when it's blocked in
 * `pthread_cond_wait`, when the cleanup code is executed the
 * `pool->deq_lock` is acquired and should be released.
 */
static void up_pool_worker_cleanup(void *arg)
{
    int retv;
    up_pool_t *pool = (up_pool_t *) arg;

    retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) {
        perror("up_pool_worker_cleanup:pthread_mutex_unlock");
    }
}

/* Dequeue a task from the pool's queue and execute it.
 *
 * This function blocks while trying to `up_pool_deq` a task from the queue.
 * When a dequeue is successfull the task's routine is executed. While the
 * task is running the thread's cancel state is set to `disabled` to ensure
 * the execution of all the client code.
 */
static void *up_pool_worker(void *arg)
{
    int retv;
    up_pool_t *pool = (up_pool_t *) arg;

    pthread_cleanup_push(up_pool_worker_cleanup, arg);

    for ( ;; ) {
        up_task_t task;

        retv = up_pool_deq(pool, &task);
        if (retv != UP_SUCCESS) {
            perror("up_pool_worker:up_pool_deq");
            pthread_exit(NULL);
        }

        /* Disable and then re-enable cancel state in order to ensure
         * `task.task_routine`'s execution. */
        retv = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if (retv != 0) {
            perror("up_pool_worker: Could not disable cancel state.");
        }

        task.task_routine(task.arg);

        retv = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if (retv != 0) {
            perror("up_pool_worker: Could not enable cancel state.");
            pthread_exit(NULL);
        }
    }

    pthread_cleanup_pop(1);

    return NULL;
}

/* Create a new thread pool.
 *
 * After allocating resources the threads are beeing created.
 */
int up_pool_create(up_pool_t **pool, size_t n)
{
    int retv;
    size_t i, t;
    up_pool_t *p;

    if (n < 1) {
        return UP_ERROR_CONF_INVAL;
    }

    p = (up_pool_t *) malloc(sizeof(up_pool_t));
    if (p == NULL) {
        up_handle_error("up_pool_create:malloc", UP_ERROR_MALLOC);
    }

    *pool = p;

    p->thread_count = n;

    p->enq_count = 0;
    p->deq_count = 0;

    p->threads = (pthread_t *) malloc(n * sizeof(pthread_t));
    if (p->threads == NULL) {
        up_handle_error("up_pool_create:malloc", UP_ERROR_MALLOC);
    }

    pthread_cond_init(&p->cond, NULL);

    pthread_mutex_init(&p->enq_lock, NULL);
    pthread_mutex_init(&p->deq_lock, NULL);

    p->head = (up_node_t *) calloc(1, sizeof(up_node_t));
    if (p->head == NULL) {
        up_handle_error("up_pool_create:calloc", UP_ERROR_MALLOC);
    }

    p->tail = p->head;

    for (i = 0; i < n; i++) {
        retv = pthread_create(&p->threads[i], NULL, up_pool_worker, p);
        if (retv != 0) {
            up_handle_error("up_pool_create:pthread_create", UP_ERROR_THREAD_CREATE);
        }
    }

    do {
        retv = pthread_mutex_lock(&p->deq_lock);
        if (retv != 0) {
            up_handle_error("up_pool_create:pthread_mutex_lock", UP_ERROR_MUTEX_LOCK);
        }

        t = p->deq_count;

        retv = pthread_mutex_unlock(&p->deq_lock);
        if (retv != 0) {
            up_handle_error("up_pool_create:pthread_mutex_unlock", UP_ERROR_MUTEX_LOCK);
        }
    } while (t != n);

    p->deq_count = 0;

    return UP_SUCCESS;
}

/* Destroy the thread pool.
 *
 * First try to `pthread_cancel` all the threads and then `pthread_join`
 * them to ensure they have terminated. Then release allocated resources.
 */
int up_pool_destroy(up_pool_t *pool)
{
    int retv;
    size_t i;
    up_node_t *c, *t;

    for (i = 0; i < pool->thread_count; i++) {
        retv = pthread_cancel(pool->threads[i]);
        if (retv != 0) {
            perror("up_pool_destroy:pthread_cancel");
        }
    }

    for (i = 0; i < pool->thread_count; i++) {
        retv = pthread_join(pool->threads[i], NULL);
        if (retv != 0) {
            up_handle_error("up_pool_destroy:pthread_join", UP_ERROR_THREAD_JOIN);
        }
    }

    free(pool->threads);

    retv = pthread_cond_destroy(&pool->cond);
    if (retv != 0) {
        up_handle_error("up_pool_destroy:pthread_cond_destroy", UP_ERROR_COND_DESTROY);
    }

    retv = pthread_mutex_destroy(&pool->enq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_destroy:pthread_mutex_destroy", UP_ERROR_MUTEX_DESTROY);
    }

    retv = pthread_mutex_destroy(&pool->deq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_destroy:pthread_mutex_destroy", UP_ERROR_MUTEX_DESTROY);
    }

    for (c = pool->head; c != NULL; ) {
        t = c;
        c = c->next;
        free(t);
    }

    free(pool);

    return UP_SUCCESS;
}

/* Submit a new task to the pool's queue. */
int up_pool_submit(up_pool_t *pool, void (*task_routine) (void *), void *arg)
{
    int retv;
    up_task_t task;

    task.task_routine = task_routine;
    task.arg = arg;

    retv = up_pool_enq(pool, &task);

    return retv;
}

/* Return the number of enqueued tasks (not yet executed). */
int up_pool_queue_size(up_pool_t *pool, size_t *size)
{
    int retv;

    retv = pthread_mutex_lock(&pool->enq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_queue_size:pthread_mutex_lock", UP_ERROR_MUTEX_LOCK);
    }

    retv = pthread_mutex_lock(&pool->deq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_queue_size:pthread_mutex_lock", UP_ERROR_MUTEX_LOCK);
    }

    *size = pool->enq_count - pool->deq_count;

    retv = pthread_mutex_unlock(&pool->enq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_queue_size:pthread_mutex_unlock", UP_ERROR_MUTEX_LOCK);
    }

    retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) {
        up_handle_error("up_pool_queue_size:pthread_mutex_unlock", UP_ERROR_MUTEX_LOCK);
    }

    return UP_SUCCESS;
}
