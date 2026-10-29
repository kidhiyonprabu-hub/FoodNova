/*
 * FoodRush – Smart Online Food Ordering System
 * threadpool.c – Bounded worker-pool implementation
 *
 * Design: bounded circular queue + condition-variable signalling.
 * Workers block on pthread_cond_wait when the queue is empty.
 * Producers block on pthread_cond_wait when the queue is full.
 * Statistics are protected by a separate mutex to minimise contention.
 */

#include "threadpool.h"
#include <errno.h>

/* ── Internal structures ─────────────────────────────────────────────────── */

typedef struct {
    pthread_t  tid;
    int        id;          /* 1-based worker index */
    int        busy;        /* 1 when executing a task */
    thread_pool_t *pool;
} worker_t;

struct thread_pool {
    worker_t      *workers;
    int            num_workers;

    task_t        *queue;
    int            queue_cap;
    int            queue_head;   /* next slot to consume */
    int            queue_tail;   /* next slot to fill    */
    int            queue_size;

    pthread_mutex_t q_mutex;
    pthread_cond_t  q_not_empty;
    pthread_cond_t  q_not_full;

    /* statistics */
    pthread_mutex_t stats_mutex;
    int  completed;
    int  failed;

    /* lifecycle */
    int  shutdown;
};

/* ── Worker thread ──────────────────────────────────────────────────────── */

static void *worker_func(void *arg)
{
    worker_t      *w    = (worker_t *)arg;
    thread_pool_t *pool = w->pool;

    LOG_INFO("Worker-%d started (TID=%lu)", w->id, (unsigned long)pthread_self());

    while (1) {
        pthread_mutex_lock(&pool->q_mutex);

        /* Wait for work or shutdown */
        while (pool->queue_size == 0 && !pool->shutdown)
            pthread_cond_wait(&pool->q_not_empty, &pool->q_mutex);

        if (pool->shutdown && pool->queue_size == 0) {
            pthread_mutex_unlock(&pool->q_mutex);
            break;
        }

        /* Dequeue */
        task_t task = pool->queue[pool->queue_head];
        pool->queue_head = (pool->queue_head + 1) % pool->queue_cap;
        pool->queue_size--;
        w->busy = 1;

        pthread_cond_signal(&pool->q_not_full);
        pthread_mutex_unlock(&pool->q_mutex);

        /* Execute */
        int ok = 1;
        if (task.fn) {
            /* Wrap in a simple error guard */
#ifdef _WIN32
            task.fn(task.arg);
#else
            task.fn(task.arg);
#endif
        } else {
            ok = 0;
        }

        pthread_mutex_lock(&pool->stats_mutex);
        if (ok) pool->completed++; else pool->failed++;
        pthread_mutex_unlock(&pool->stats_mutex);

        pthread_mutex_lock(&pool->q_mutex);
        w->busy = 0;
        pthread_mutex_unlock(&pool->q_mutex);
    }

    LOG_INFO("Worker-%d exiting", w->id);
    return NULL;
}

/* ── Public API ─────────────────────────────────────────────────────────── */

thread_pool_t *pool_create(int num_workers, int queue_cap)
{
    if (num_workers <= 0 || queue_cap <= 0) return NULL;

    thread_pool_t *pool = (thread_pool_t *)calloc(1, sizeof(thread_pool_t));
    if (!pool) return NULL;

    pool->num_workers = num_workers;
    pool->queue_cap   = queue_cap;
    pool->queue       = (task_t *)calloc(queue_cap, sizeof(task_t));
    pool->workers     = (worker_t *)calloc(num_workers, sizeof(worker_t));

    if (!pool->queue || !pool->workers) goto fail;

    pthread_mutex_init(&pool->q_mutex,     NULL);
    pthread_mutex_init(&pool->stats_mutex, NULL);
    pthread_cond_init(&pool->q_not_empty,  NULL);
    pthread_cond_init(&pool->q_not_full,   NULL);

    for (int i = 0; i < num_workers; i++) {
        pool->workers[i].id   = i + 1;
        pool->workers[i].pool = pool;
        if (pthread_create(&pool->workers[i].tid, NULL, worker_func,
                           &pool->workers[i]) != 0) {
            LOG_ERR("Failed to create worker thread %d", i + 1);
            goto fail;
        }
    }

    LOG_INFO("Thread pool created: %d workers, queue cap=%d",
             num_workers, queue_cap);
    return pool;

fail:
    free(pool->queue);
    free(pool->workers);
    free(pool);
    return NULL;
}

void pool_destroy(thread_pool_t *pool)
{
    if (!pool) return;

    pthread_mutex_lock(&pool->q_mutex);
    pool->shutdown = 1;
    pthread_cond_broadcast(&pool->q_not_empty);
    pthread_mutex_unlock(&pool->q_mutex);

    for (int i = 0; i < pool->num_workers; i++)
        pthread_join(pool->workers[i].tid, NULL);

    pthread_mutex_destroy(&pool->q_mutex);
    pthread_mutex_destroy(&pool->stats_mutex);
    pthread_cond_destroy(&pool->q_not_empty);
    pthread_cond_destroy(&pool->q_not_full);

    free(pool->queue);
    free(pool->workers);
    free(pool);
    LOG_INFO("Thread pool destroyed");
}

int pool_submit(thread_pool_t *pool, task_fn_t fn, void *arg)
{
    if (!pool || !fn) return -1;

    pthread_mutex_lock(&pool->q_mutex);

    /* Non-blocking: reject if full */
    if (pool->queue_size >= pool->queue_cap) {
        pthread_mutex_unlock(&pool->q_mutex);
        LOG_ERR("Task queue full – rejecting task");
        return -1;
    }

    pool->queue[pool->queue_tail].fn  = fn;
    pool->queue[pool->queue_tail].arg = arg;
    pool->queue_tail = (pool->queue_tail + 1) % pool->queue_cap;
    pool->queue_size++;

    pthread_cond_signal(&pool->q_not_empty);
    pthread_mutex_unlock(&pool->q_mutex);
    return 0;
}

void pool_stats(thread_pool_t *pool, pool_stats_t *out)
{
    if (!pool || !out) return;
    memset(out, 0, sizeof(*out));

    pthread_mutex_lock(&pool->q_mutex);
    out->queued_tasks = pool->queue_size;
    for (int i = 0; i < pool->num_workers; i++) {
        if (pool->workers[i].busy) out->active_workers++;
        else                       out->idle_workers++;
    }
    pthread_mutex_unlock(&pool->q_mutex);

    pthread_mutex_lock(&pool->stats_mutex);
    out->completed_tasks = pool->completed;
    out->failed_tasks    = pool->failed;
    pthread_mutex_unlock(&pool->stats_mutex);
}

void pool_wait(thread_pool_t *pool)
{
    if (!pool) return;
    /* Spin-wait until queue drains and all workers are idle */
    while (1) {
        pthread_mutex_lock(&pool->q_mutex);
        int sz   = pool->queue_size;
        int busy = 0;
        for (int i = 0; i < pool->num_workers; i++)
            if (pool->workers[i].busy) busy++;
        pthread_mutex_unlock(&pool->q_mutex);
        if (sz == 0 && busy == 0) break;
#ifdef _WIN32
        Sleep(10);
#else
        struct timespec ts = {0, 10000000};
        nanosleep(&ts, NULL);
#endif
    }
}
