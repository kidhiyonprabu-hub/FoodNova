/*
 * FoodRush – Smart Online Food Ordering System
 * threadpool.h – Bounded worker-pool interface
 */
#ifndef THREADPOOL_H
#define THREADPOOL_H

#include "common.h"

/* Task function signature */
typedef void (*task_fn_t)(void *arg);

/* A single task */
typedef struct {
    task_fn_t fn;
    void     *arg;
} task_t;

/* Worker pool statistics */
typedef struct {
    int active_workers;
    int idle_workers;
    int queued_tasks;
    int completed_tasks;
    int failed_tasks;
} pool_stats_t;

/* Opaque pool handle */
typedef struct thread_pool thread_pool_t;

/* Create / destroy */
thread_pool_t *pool_create(int num_workers, int queue_cap);
void           pool_destroy(thread_pool_t *pool);

/* Submit a task; returns 0 on success, -1 if queue full */
int  pool_submit(thread_pool_t *pool, task_fn_t fn, void *arg);

/* Snapshot of current statistics */
void pool_stats(thread_pool_t *pool, pool_stats_t *out);

/* Wait until the task queue drains */
void pool_wait(thread_pool_t *pool);

#endif /* THREADPOOL_H */
