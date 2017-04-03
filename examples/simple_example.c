#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>

#include "../src/upool.h"

void largest_prime_naive(void *arg);

int main()
{
    size_t i;
    int *args;
    up_pool_t *pool;

    const size_t INPUT_SIZE = 30;
    const size_t THREAD_COUNT = 8;

    /* Prepare arguments. */
    args = (int *) calloc(INPUT_SIZE, sizeof(unsigned int));
    for (i = 0; i < INPUT_SIZE; i++) {
        args[i] = rand() % 1000;
    }

    /* Create Pool. */
    up_pool_create(&pool, THREAD_COUNT);

    /* Submit work to Pool. */
    for (i = 0; i < INPUT_SIZE; i++) {
        up_pool_submit(pool, largest_prime_naive, (void *) &args[i]);
    }

    /* Wait for Pool. */
    up_pool_wait(pool);

    /* Release Pool's locks. */
    up_pool_release(pool);

    /* Destroy Pool. */
    up_pool_destroy(pool);

    return 0;
}

/* Find the largest prime less than or equal to `n`. */
void largest_prime_naive(void *arg)
{
    size_t i, j;
    int n = *(int *) arg;

    for (i = n; i > 0; i--) {
        j = 2;

        for ( ; i % j != 0; j++) { }

        if (j == i) {
            printf("(%d, %ld)\n", n, j);
            break;
        }
    }
}

