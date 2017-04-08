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

#define UP_SUCCESS 0
#define UP_ERROR_MALLOC -1
#define UP_ERROR_THREAD_CREATE -2
#define UP_ERROR_THREAD_JOIN -3
#define UP_ERROR_MUTEX_LOCK -4
#define UP_ERROR_MUTEX_BUSY -5
#define UP_ERROR_MUTEX_DESTROY -6
#define UP_ERROR_COND_DESTROY -7
#define UP_ERROR_CONF_INVAL -8

/* The thread pool. */
typedef struct up_pool up_pool_t;

/* Create a new thread pool. */
int up_pool_create(up_pool_t **pool, size_t n);

/* Destroy the thread pool. */
int up_pool_destroy(up_pool_t *pool);

/* Submit a new task to the pool's queue. Blocks until the task is enqueued. */
int up_pool_submit(up_pool_t *pool, void (*task_routine) (void *), void *arg);

/* Return the number of enqueued tasks (not yet executed). */
int up_pool_queue_size(up_pool_t *pool, size_t *size);

#endif
