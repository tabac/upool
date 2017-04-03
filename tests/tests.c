#include <unistd.h>

#include "../src/upool.c"


#define assert_equals(a, b) \
    do { if (a != b) return 1; } while (0)

#define assert_not_equals(a, b) \
    do { if (a == b) return 1; } while (0)

typedef struct TestConsumerContext {
    int out;
    pthread_t thread_id;
    pthread_cond_t cond;
    pthread_mutex_t lock;
} TestConsumerContext;

void run(char *desc,
         int (*test_routine) (void *),
         void *(*test_setup) (),
         void (test_teardown) (void *));

void *setup_pool();
void teardown_pool(void *context);

int test_pool_enq_deq_locked(void *context);
int test_pool_deq_enq_locked(void *context);
int test_pool_destroy_during_execution(void *context);
int test_pool_submit_lock_fails(void *context);

void consumer_routine(void *arg);
void consumer_routine_sleeper(void *arg);

int main()
{
    run("test_pool_enq_deq_locked",
        test_pool_enq_deq_locked,
        setup_pool,
        teardown_pool);

    run("test_pool_deq_enq_locked",
        test_pool_deq_enq_locked,
        setup_pool,
        teardown_pool);

    run("test_pool_submit_lock_fails",
        test_pool_submit_lock_fails,
        setup_pool,
        teardown_pool);

    run("test_pool_destroy_during_execution",
        test_pool_destroy_during_execution,
        setup_pool,
        NULL);

    return 0;
}

void *setup_pool()
{
    /* Create pool. */
    up_pool_t *pool = NULL;
    up_pool_create(&pool, 4);

    /* Wait for all the threads to be spawned and ready. */
    for ( ;; ) {
        pthread_mutex_lock(&pool->deq_lock);

        if (pool->thread_count != pool->deq_count) {
            pthread_mutex_unlock(&pool->deq_lock);
        } else {
            pthread_mutex_unlock(&pool->deq_lock);
            break;
        }
    }

    return (void *) pool;
}

void teardown_pool(void *context)
{
    up_pool_t *pool = (up_pool_t *) context;

    up_pool_destroy(pool);
}

void consumer_routine(void *arg)
{
}

int test_pool_enq_deq_locked(void *context)
{
    int retv;
    up_pool_t *pool = (up_pool_t *) context;

    /* Block task execution. */
    pthread_mutex_lock(&pool->deq_lock);

    retv = up_pool_submit(pool, consumer_routine, NULL);

    /* Assert submit was succesful. */
    assert_equals(retv, UP_SUCCESS);

    /* Assert enq counter updated. */
    assert_equals(pool->enq_count, 1);

    /* Unblock task execution. */
    pthread_mutex_unlock(&pool->deq_lock);

    /* Wait for tasks to finish without counter reset. */
    pthread_mutex_lock(&pool->enq_lock);

    for ( ;; ) {
        pthread_mutex_lock(&pool->deq_lock);

        if (pool->thread_count + pool->enq_count != pool->deq_count) {
            pthread_mutex_unlock(&pool->deq_lock);
        } else {
            break;
        }
    }

    /* Release locks. */
    up_pool_release(pool);

    /* Assert deq counter updated. */
    assert_equals(pool->deq_count, 5);

    return 0;
}

int test_pool_deq_enq_locked(void *context)
{
    int retv;
    up_pool_t *pool = (up_pool_t *) context;

    /* Block task execution. */
    pthread_mutex_lock(&pool->deq_lock);

    retv = up_pool_submit(pool, consumer_routine, NULL);

    /* Assert submit was succesful. */
    assert_equals(retv, UP_SUCCESS);

    /* Assert enq counter updated. */
    assert_equals(pool->enq_count, 1);

    /* Block task submition. */
    pthread_mutex_lock(&pool->enq_lock);

    /* Unblock task execution. */
    pthread_mutex_unlock(&pool->deq_lock);

    /* Wait for tasks to finish without counter reset. */
    for ( ;; ) {
        pthread_mutex_lock(&pool->deq_lock);

        if (pool->thread_count + pool->enq_count != pool->deq_count) {
            pthread_mutex_unlock(&pool->deq_lock);
        } else {
            break;
        }
    }

    /* Assert deq counter updated. */
    assert_equals(pool->deq_count, 5);

    /* Release locks. */
    up_pool_release(pool);

    return 0;
}

void consumer_routine_sleeper(void *arg)
{
    TestConsumerContext *c = (TestConsumerContext *) arg;

    /* Store thread_id to context. */
    c->thread_id = pthread_self();

    /* Allow the producer to continue. This task is in the
     * middle of its execution. */
    pthread_mutex_lock(&c->lock);
    c->out = 2;
    pthread_cond_signal(&c->cond);
    pthread_mutex_unlock(&c->lock);

    /* Wait until the producer sends a cancel signal to this task. */
    pthread_mutex_lock(&c->lock);
    while (c->out != 3) {
        pthread_cond_wait(&c->cond, &c->lock);
    }

    /* Store an output value to ensure correct execution. */
    c->out = 0;

    pthread_mutex_unlock(&c->lock);
}

int test_pool_destroy_during_execution(void *context)
{
    int retv;
    size_t i;
    void *retval;
    up_node_t *n, *t;
    up_pool_t *pool = (up_pool_t *) context;

    /* Initialize TestConsumerContext.
     *
     * The state of the two threads is expressed through `out`'s values.
     *
     * `out == 1` -> The consumer thread is starting.
     * `out == 2` -> The consumer thread has started and is waiting for
     *               the producer to send the cancel signal.
     * `out == 3` -> The producer has sent the cancel signal, the consumer
     *               is about to finish executing. */
    TestConsumerContext c;

    c.out = 1;
    pthread_cond_init(&c.cond, NULL);
    pthread_mutex_init(&c.lock, NULL);

    /* Submit a task that will block. */
    retv = up_pool_submit(pool, consumer_routine_sleeper, (void *) &c);
    assert_equals(retv, UP_SUCCESS);

    /* Wait for task to start. */
    pthread_mutex_lock(&c.lock);
    while (c.out != 2) {
        pthread_cond_wait(&c.cond, &c.lock);
    }

    /* Send cancel signal to thread. */
    retv = pthread_cancel(c.thread_id);
    assert_equals(retv, 0);

    /* Allow task to terminate. */
    c.out = 3;
    pthread_cond_signal(&c.cond);
    pthread_mutex_unlock(&c.lock);

    /* Assert that the task was cancelled. */
    retv = pthread_join(c.thread_id, &retval);
    assert_equals(retv, 0);
    assert_equals(retval, PTHREAD_CANCELED);

    /* Assert that the routine terminated succesfully. */
    assert_equals(c.out, 0);

    /* Try to destroy the pool. */
    retv = up_pool_destroy(pool);
    assert_equals(retv, UP_ERROR_THREAD_JOIN);

    /* Cleanup since `up_pool_destroy` failed. */
    for (i = 0; i < pool->thread_count; i++) {
        retv = pthread_join(pool->threads[i], NULL);
    }

    free(pool->threads);

    pthread_cond_destroy(&pool->cond);
    pthread_mutex_destroy(&pool->enq_lock);
    pthread_mutex_destroy(&pool->deq_lock);

    for (n = pool->head; n != NULL; ) {
        t = n;
        n = n->next;
        free(t);
    }

    free(pool);

    /* Cleanup TestConsumerContext. */
    pthread_cond_destroy(&c.cond);
    pthread_mutex_destroy(&c.lock);

    return 0;
}

int test_pool_submit_lock_fails(void *context)
{
    int retv;
    up_pool_t *pool = (up_pool_t *) context;

    /* Destroy enq lock. */
    pthread_mutex_destroy(&pool->enq_lock);

    /* Try to submita task, should fail. */
    retv = up_pool_submit(pool, consumer_routine, NULL);
    assert_equals(retv, UP_ERROR_MUTEX_LOCK);

    /* Assert nothing was added to the list. */
    assert_equals(pool->head->next, NULL);

    /* Re-init mutex for teardown to work. */
    pthread_mutex_init(&pool->enq_lock, NULL);

    return 0;
}

void run(char *desc,
         int (*test_routine) (void *),
         void *(*test_setup) (),
         void (test_teardown) (void *))
{
    void *context = NULL;

    printf("%s...", desc);

    /* Run setup routine. */
    if (test_setup != NULL) {
        context = test_setup();
    }

    /* Run test and print status. */
    if (test_routine(context) != 0) {
        printf("fail\n");
    } else {
        printf("ok\n");
    }

    /* Run teardown routine. */
    if (test_teardown != NULL) {
        test_teardown(context);
    }
}
