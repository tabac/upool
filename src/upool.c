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

#include <errno.h>

#include "upool.h"

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
    up_node_t *node = (up_node_t *) malloc(sizeof(up_node_t));
    if (node == NULL) { up_handle_error_return("up_pool_enq", UP_ERROR_MALLOC); }

    memcpy((void *) &node->task, (const void *) task, sizeof(up_task_t));
    node->next = NULL;

    int retv = pthread_mutex_lock(&pool->enq_lock);
    if (retv != 0) {
        free(node);
        up_handle_error_en_return("up_pool_enq", retv, retv);
    }

    pool->tail->next = node;
    pool->tail = node;

    pool->enq_count += 1;

    retv = pthread_mutex_unlock(&pool->enq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_enq", retv); }

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
    int retv = pthread_mutex_lock(&pool->deq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_deq", retv); }

    pool->deq_count += 1;

    while (pool->head->next == NULL) {
        /* If the queue is empty block until a producer thread enqueues
         * a task and signals the condition.
         *
         * This is the only Cancellation Point of a consumer thread.
         * From here it's safe to jump to `up_pool_worker_cleanup`. */
        pthread_cond_wait(&pool->cond, &pool->deq_lock);
    }

    up_node_t *old_head = pool->head;

    pool->head = pool->head->next;

    memcpy((void *) task, (const void *) &pool->head->task, sizeof(up_task_t));

    memset((void *) &pool->head->task, 0, sizeof(up_task_t));

    retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_deq", retv); }

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
    up_pool_t *pool = (up_pool_t *) arg;

    int retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) { perror("up_pool_worker_cleanup"); }
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
    up_pool_t *pool = (up_pool_t *) arg;

    pthread_cleanup_push(up_pool_worker_cleanup, arg);

    for ( ;; ) {
        up_task_t task;

        int retv = up_pool_deq(pool, &task);
        if (retv != UP_SUCCESS) {
            perror("up_pool_worker");

            int *retval = (int *) malloc(sizeof(int));
            if (retval != NULL) {
                *retval = retv;
            }
            pthread_exit((void *) retval);
        }

        /* Disable and then re-enable cancel state in order to ensure
         * `task.task_routine`'s execution. */
        retv = pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
        if (retv != 0) { perror("up_pool_worker: Could not disable cancel state."); }

        task.task_routine(task.arg);

        retv = pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
        if (retv != 0) {
            perror("up_pool_worker");

            int *retval = (int *) malloc(sizeof(int));
            if (retval != NULL) {
                *retval = retv;
            }
            pthread_exit((void *) retval);
        }
    }

    pthread_cleanup_pop(1);

    return NULL;
}

/* Create a new thread pool.
 *
 * After allocating resources the threads are spawned.
 */
int up_pool_create(up_pool_t **pool, size_t n)
{
    up_pool_t *p = (up_pool_t *) malloc(sizeof(up_pool_t));
    if (p == NULL) { up_handle_error_return("up_pool_create", UP_ERROR_MALLOC); }

    *pool = p;

    p->thread_count = n;

    p->enq_count = 0;
    p->deq_count = 0;

    p->threads = (pthread_t *) malloc(n * sizeof(pthread_t));
    if (p->threads == NULL) { up_handle_error_return("up_pool_create", UP_ERROR_MALLOC); }

    pthread_cond_init(&p->cond, NULL);

    pthread_mutex_init(&p->enq_lock, NULL);
    pthread_mutex_init(&p->deq_lock, NULL);

    p->head = (up_node_t *) calloc(1, sizeof(up_node_t));
    if (p->head == NULL) { up_handle_error_return("up_pool_create", UP_ERROR_MALLOC); }

    p->tail = p->head;

    for (int i = 0; i < n; i++) {
        int retv = pthread_create(&p->threads[i], NULL, up_pool_worker, p);
        if (retv != 0) { up_handle_error_return("up_pool_create", UP_ERROR_THREAD_CREATE); }
    }

    return UP_SUCCESS;
}

/* Destroy a thread pool.
 *
 * First try to `pthread_cancel` all the threads and then `pthread_join`
 * them to ensure they have terminated. Then release allocated resources.
 */
int up_pool_destroy(up_pool_t *pool)
{
    for (int i = 0; i < pool->thread_count; i++) {
        int retv = pthread_cancel(pool->threads[i]);
        if (retv != 0) { perror("up_pool_destroy"); }
    }

    for (int i = 0; i < pool->thread_count; i++) {
        int retv = pthread_join(pool->threads[i], NULL);
        if (retv != 0) { up_handle_error_return("up_pool_destroy", retv); }
    }

    free(pool->threads);

    int retv = pthread_cond_destroy(&pool->cond);
    if (retv != 0) { up_handle_error_return("up_pool_destroy", retv); }

    retv = pthread_mutex_destroy(&pool->enq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_destroy", retv); }

    retv = pthread_mutex_destroy(&pool->deq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_destroy", retv); }

    for (up_node_t *c = pool->head; c != NULL; ) {
        up_node_t *t = c;
        c = c->next;
        free(t);
    }

    free(pool);

    return UP_SUCCESS;
}

/* Submit a new task to the pool's queue. */
int up_pool_submit(up_pool_t *pool, void (*task_routine) (void *), void *arg)
{
    up_task_t task;

    task.task_routine = task_routine;
    task.arg = arg;

    int retv = up_pool_enq(pool, &task);

    return retv;
}

/* Wait for all the threads to finish their current work.
 *
 * First lock the `pool->enq_lock` so that it's not possible to submit
 * new tasks. Then busy loop until the `pool->enq_count` is equal to
 * `pool->deq_count`.
 *
 * This function exists with both queue locks in locked state.
 */
int up_pool_wait(up_pool_t *pool)
{
    int retv = pthread_mutex_lock(&pool->enq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_wait", retv); }

    for ( ;; ) {
        retv = pthread_mutex_lock(&pool->deq_lock);
        if (retv != 0) { up_handle_error_return("up_pool_wait", retv); }

        if (pool->thread_count + pool->enq_count != pool->deq_count) {
            /* TODO: When the consumer threads are first created increase
             *       the `pool->deq_count` without doing any work. Maybe
             *       make this a little better. This is why we add
             *       `pool->thread_count` here. */
            retv = pthread_mutex_unlock(&pool->deq_lock);
            if (retv != 0) { up_handle_error_return("up_pool_wait", retv); }
        } else {
            pool->enq_count = 0;
            pool->deq_count = pool->thread_count;
            break;
        }
    }

    return UP_SUCCESS;
}

/* Unlock the pool queue's locks. */
int up_pool_release(up_pool_t *pool)
{
    int retv = pthread_mutex_unlock(&pool->enq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_release", retv); }

    retv = pthread_mutex_unlock(&pool->deq_lock);
    if (retv != 0) { up_handle_error_return("up_pool_release", retv); }

    return UP_SUCCESS;
}
