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

#ifndef __UPOOL_H__
#define __UPOOL_H__

#include <pthread.h>

#define UP_SUCCESS 0
#define UP_ERROR_MALLOC -1
#define UP_ERROR_THREAD_CREATE -2

#define up_handle_error_en_return(msg, en, retv) \
    do { errno = en; perror(msg); return retv; } while (0)

#define up_handle_error_return(msg, retv) \
    do { perror(msg); return retv; } while (0)

typedef struct up_task {
    void (*task_routine) (void *);        /* Pointer to the routine to execute. */
    void *arg;                            /* Pointer to the arg of the routine. */
} up_task_t;

typedef struct up_node {
    up_task_t task;                       /* The task to be executed. */
    struct up_node *next;                 /* Pointer to the next queue node. */
} up_node_t;

typedef struct up_pool {
    size_t thread_count;                  /* Number of threads of the Pool. */
    size_t enq_count, deq_count;          /* Enqueued/Dequeued task counters. */
    pthread_t *threads;                   /* Array of thread IDs. */
    pthread_cond_t cond;                  /* Condition to signal threads for tasks. */
    pthread_mutex_t enq_lock, deq_lock;   /* Task queue's locks. */
    up_node_t *head, *tail;               /* Task queue's head, tail. */
} up_pool_t;

int up_pool_create(up_pool_t **pool, size_t n);
int up_pool_destroy(up_pool_t *pool);

int up_pool_submit(up_pool_t *pool, void (*task_routine) (void *), void *arg);

int up_pool_wait(up_pool_t *pool);
int up_pool_release(up_pool_t *pool);

#endif
