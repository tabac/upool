#include <stdio.h>
#include <stdlib.h>

#include "../src/upool.h"

typedef struct ConsumerContext {
    int in, out;
} ConsumerContext;

typedef struct ProducerContext {
    up_pool_t *pool;
    ConsumerContext *args;
    size_t tasks_count;
    size_t rank;
} ProducerContext;

void *producer_routine(void *arg);

void consumer_routine_id(void *arg);
void consumer_routine_prime(void *arg);
void consumer_routine_opposite(void *arg);

int main()
{
    const unsigned int INPUT_SIZE = 100;
    const unsigned int CONSUMER_THREAD_COUNT = 10;
    const unsigned int PRODUCER_THREAD_COUNT = 10;
    const unsigned int PRODUCER_TASK_COUNT = 10;

    // Prepare ConsumerContext.
    ConsumerContext *c = (ConsumerContext *) calloc(INPUT_SIZE, sizeof(ConsumerContext));
    for (int i = 0; i < INPUT_SIZE; i++) {
        c[i].in = rand() % 1000;
    }

    // Create Pool
    up_pool_t *pool = NULL;
    up_pool_create(&pool, CONSUMER_THREAD_COUNT);

    // Prepare ProducerContext
    ProducerContext *p = (ProducerContext *) malloc(PRODUCER_THREAD_COUNT * sizeof(ProducerContext));
    for (int i = 0; i < PRODUCER_THREAD_COUNT; i++) {
        p[i].pool = pool;
        p[i].args = c;
        p[i].tasks_count = PRODUCER_TASK_COUNT;
        p[i].rank = i;
    }

    // Spawn producer threads.
    pthread_t producer_threads[PRODUCER_THREAD_COUNT];
    for (int i = 0; i < PRODUCER_THREAD_COUNT; i++) {
        pthread_create(&producer_threads[i], NULL, producer_routine, (void *) &p[i]);
    }

    // Wait for producer threads.
    for (int i = 0; i < PRODUCER_THREAD_COUNT; i++) {
        pthread_join(producer_threads[i], NULL);
    }

    // Wait for Pool.
    up_pool_wait(pool);

    for (int i = 0; i < INPUT_SIZE; i++) {
        printf("(%d, %d)\n", c[i].in, c[i].out);
    }

    // Release Pool's locks.
    up_pool_release(pool);

    // Destroy Pool.
    up_pool_destroy(pool);

    free(p);
    free(c);

    return 0;
}

/* Submit `PRODUCER_TASK_COUNT` tasks to the pool. */
void *producer_routine(void *arg)
{
    ProducerContext *c = (ProducerContext *) arg;

    unsigned int offset = c->rank * c->tasks_count;
    for (int i = 0; i < c->tasks_count; i++) {
        ConsumerContext *a = &c->args[offset + i];

        switch (i % 3) {
            case 0:
                up_pool_submit(c->pool, consumer_routine_id, (void *) a);
                break;
            case 1:
                up_pool_submit(c->pool, consumer_routine_prime, (void *) a);
                break;
            case 2:
                up_pool_submit(c->pool, consumer_routine_opposite, (void *) a);
                break;
        }
    }

    return NULL;
}

/* Return the input number. */
void consumer_routine_id(void *arg)
{
    ConsumerContext *c = (ConsumerContext *) arg;

    c->out = c->in;
}

/* Return the largest prime less than or equal to the input number. */
void consumer_routine_prime(void *arg)
{
    ConsumerContext *c = (ConsumerContext *) arg;

    for (int i = c->in; i > 0; i--) {
        int j = 2;

        for ( ; i % j != 0; j++) { }

        if (j == i) {
            c->out = j;
            break;
        }
    }
}

/* Return the opposite of the input number. */
void consumer_routine_opposite(void *arg)
{
    ConsumerContext *c = (ConsumerContext *) arg;

    c->out = -1 * c->in;
}
