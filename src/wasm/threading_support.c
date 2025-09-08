/*
 * libgit2.wasm Threading Support
 * Copyright 2025 Superstruct Ltd, New Zealand
 * Licensed under GPL v2 with Linking Exception (same as libgit2)
 */

#include <git2.h>
#include <emscripten.h>
#include <emscripten/threading.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#ifdef USE_THREADS

/* Thread pool for Git operations */
#define MAX_WORKER_THREADS 4
#define TASK_QUEUE_SIZE 64

typedef enum {
    TASK_TYPE_NONE = 0,
    TASK_TYPE_HASH_OBJECT,
    TASK_TYPE_COMPRESS_DATA,
    TASK_TYPE_DECOMPRESS_DATA,
    TASK_TYPE_VALIDATE_OID
} task_type_t;

typedef struct {
    task_type_t type;
    void* input_data;
    size_t input_size;
    void* output_data;
    size_t output_size;
    int result;
    atomic_int completed;
    pthread_cond_t completion_cond;
    pthread_mutex_t completion_mutex;
} git_task_t;

typedef struct {
    git_task_t tasks[TASK_QUEUE_SIZE];
    atomic_int head;
    atomic_int tail;
    atomic_int count;
    pthread_mutex_t queue_mutex;
    pthread_cond_t work_available;
    pthread_cond_t queue_not_full;
} task_queue_t;

typedef struct {
    pthread_t threads[MAX_WORKER_THREADS];
    task_queue_t queue;
    atomic_int shutdown;
    int num_threads;
    int initialized;
} thread_pool_t;

static thread_pool_t git_thread_pool = {0};

/* Worker thread function */
static void* worker_thread(void* arg) {
    (void)arg; /* Unused parameter */
    
    while (!atomic_load(&git_thread_pool.shutdown)) {
        git_task_t* task = NULL;
        
        /* Get task from queue */
        pthread_mutex_lock(&git_thread_pool.queue.queue_mutex);
        
        while (atomic_load(&git_thread_pool.queue.count) == 0 && 
               !atomic_load(&git_thread_pool.shutdown)) {
            pthread_cond_wait(&git_thread_pool.queue.work_available, 
                            &git_thread_pool.queue.queue_mutex);
        }
        
        if (atomic_load(&git_thread_pool.shutdown)) {
            pthread_mutex_unlock(&git_thread_pool.queue.queue_mutex);
            break;
        }
        
        /* Dequeue task */
        int head = atomic_load(&git_thread_pool.queue.head);
        task = &git_thread_pool.queue.tasks[head];
        atomic_store(&git_thread_pool.queue.head, (head + 1) % TASK_QUEUE_SIZE);
        atomic_fetch_sub(&git_thread_pool.queue.count, 1);
        
        pthread_cond_signal(&git_thread_pool.queue.queue_not_full);
        pthread_mutex_unlock(&git_thread_pool.queue.queue_mutex);
        
        /* Execute task */
        switch (task->type) {
            case TASK_TYPE_HASH_OBJECT:
                task->result = git_wasm_thread_hash_object(
                    (const char*)task->input_data,
                    task->input_size,
                    (char*)task->output_data
                );
                break;
                
            case TASK_TYPE_COMPRESS_DATA:
                task->result = git_wasm_thread_compress_data(
                    task->input_data,
                    task->input_size,
                    task->output_data,
                    &task->output_size
                );
                break;
                
            case TASK_TYPE_DECOMPRESS_DATA:
                task->result = git_wasm_thread_decompress_data(
                    task->input_data,
                    task->input_size,
                    task->output_data,
                    &task->output_size
                );
                break;
                
            case TASK_TYPE_VALIDATE_OID:
                task->result = git_wasm_thread_validate_oid((const char*)task->input_data);
                break;
                
            default:
                task->result = -1;
                break;
        }
        
        /* Mark task as completed */
        pthread_mutex_lock(&task->completion_mutex);
        atomic_store(&task->completed, 1);
        pthread_cond_signal(&task->completion_cond);
        pthread_mutex_unlock(&task->completion_mutex);
    }
    
    return NULL;
}

/* Initialize thread pool */
EMSCRIPTEN_KEEPALIVE
int git_wasm_threading_init(void) {
    if (git_thread_pool.initialized) return 0;
    
    /* Initialize queue */
    atomic_init(&git_thread_pool.queue.head, 0);
    atomic_init(&git_thread_pool.queue.tail, 0);
    atomic_init(&git_thread_pool.queue.count, 0);
    
    if (pthread_mutex_init(&git_thread_pool.queue.queue_mutex, NULL) != 0) {
        return -1;
    }
    
    if (pthread_cond_init(&git_thread_pool.queue.work_available, NULL) != 0) {
        pthread_mutex_destroy(&git_thread_pool.queue.queue_mutex);
        return -1;
    }
    
    if (pthread_cond_init(&git_thread_pool.queue.queue_not_full, NULL) != 0) {
        pthread_mutex_destroy(&git_thread_pool.queue.queue_mutex);
        pthread_cond_destroy(&git_thread_pool.queue.work_available);
        return -1;
    }
    
    /* Initialize task completion synchronization */
    for (int i = 0; i < TASK_QUEUE_SIZE; i++) {
        pthread_mutex_init(&git_thread_pool.queue.tasks[i].completion_mutex, NULL);
        pthread_cond_init(&git_thread_pool.queue.tasks[i].completion_cond, NULL);
    }
    
    /* Initialize shutdown flag */
    atomic_init(&git_thread_pool.shutdown, 0);
    
    /* Create worker threads */
    git_thread_pool.num_threads = emscripten_num_logical_cores();
    if (git_thread_pool.num_threads > MAX_WORKER_THREADS) {
        git_thread_pool.num_threads = MAX_WORKER_THREADS;
    }
    
    for (int i = 0; i < git_thread_pool.num_threads; i++) {
        if (pthread_create(&git_thread_pool.threads[i], NULL, worker_thread, NULL) != 0) {
            /* Cleanup on failure */
            atomic_store(&git_thread_pool.shutdown, 1);
            
            for (int j = 0; j < i; j++) {
                pthread_join(git_thread_pool.threads[j], NULL);
            }
            
            return -1;
        }
    }
    
    git_thread_pool.initialized = 1;
    return 0;
}

/* Shutdown thread pool */
EMSCRIPTEN_KEEPALIVE
void git_wasm_threading_cleanup(void) {
    if (!git_thread_pool.initialized) return;
    
    /* Signal shutdown */
    atomic_store(&git_thread_pool.shutdown, 1);
    
    /* Wake up all worker threads */
    pthread_mutex_lock(&git_thread_pool.queue.queue_mutex);
    pthread_cond_broadcast(&git_thread_pool.queue.work_available);
    pthread_mutex_unlock(&git_thread_pool.queue.queue_mutex);
    
    /* Wait for all threads to complete */
    for (int i = 0; i < git_thread_pool.num_threads; i++) {
        pthread_join(git_thread_pool.threads[i], NULL);
    }
    
    /* Cleanup synchronization objects */
    pthread_mutex_destroy(&git_thread_pool.queue.queue_mutex);
    pthread_cond_destroy(&git_thread_pool.queue.work_available);
    pthread_cond_destroy(&git_thread_pool.queue.queue_not_full);
    
    for (int i = 0; i < TASK_QUEUE_SIZE; i++) {
        pthread_mutex_destroy(&git_thread_pool.queue.tasks[i].completion_mutex);
        pthread_cond_destroy(&git_thread_pool.queue.tasks[i].completion_cond);
    }
    
    git_thread_pool.initialized = 0;
}

/* Submit task to thread pool */
static git_task_t* submit_task(task_type_t type, void* input_data, size_t input_size,
                              void* output_data, size_t output_size) {
    if (!git_thread_pool.initialized) {
        if (git_wasm_threading_init() != 0) return NULL;
    }
    
    /* Wait for queue space */
    pthread_mutex_lock(&git_thread_pool.queue.queue_mutex);
    
    while (atomic_load(&git_thread_pool.queue.count) >= TASK_QUEUE_SIZE) {
        pthread_cond_wait(&git_thread_pool.queue.queue_not_full,
                         &git_thread_pool.queue.queue_mutex);
    }
    
    /* Enqueue task */
    int tail = atomic_load(&git_thread_pool.queue.tail);
    git_task_t* task = &git_thread_pool.queue.tasks[tail];
    
    task->type = type;
    task->input_data = input_data;
    task->input_size = input_size;
    task->output_data = output_data;
    task->output_size = output_size;
    task->result = 0;
    atomic_store(&task->completed, 0);
    
    atomic_store(&git_thread_pool.queue.tail, (tail + 1) % TASK_QUEUE_SIZE);
    atomic_fetch_add(&git_thread_pool.queue.count, 1);
    
    pthread_cond_signal(&git_thread_pool.queue.work_available);
    pthread_mutex_unlock(&git_thread_pool.queue.queue_mutex);
    
    return task;
}

/* Wait for task completion */
static int wait_for_task(git_task_t* task, int timeout_ms) {
    if (!task) return -1;
    
    pthread_mutex_lock(&task->completion_mutex);
    
    if (timeout_ms > 0) {
        struct timespec abs_timeout;
        clock_gettime(CLOCK_REALTIME, &abs_timeout);
        abs_timeout.tv_sec += timeout_ms / 1000;
        abs_timeout.tv_nsec += (timeout_ms % 1000) * 1000000;
        
        while (!atomic_load(&task->completed)) {
            int wait_result = pthread_cond_timedwait(&task->completion_cond,
                                                   &task->completion_mutex,
                                                   &abs_timeout);
            if (wait_result == ETIMEDOUT) {
                pthread_mutex_unlock(&task->completion_mutex);
                return -2; /* Timeout */
            }
        }
    } else {
        /* Wait indefinitely */
        while (!atomic_load(&task->completed)) {
            pthread_cond_wait(&task->completion_cond, &task->completion_mutex);
        }
    }
    
    int result = task->result;
    pthread_mutex_unlock(&task->completion_mutex);
    
    return result;
}

/* Threading-enabled Git operations */

EMSCRIPTEN_KEEPALIVE
int git_wasm_thread_hash_object(const char* data, size_t size, char* oid_hex) {
    /* Simplified hash calculation (would use actual Git object hashing) */
    if (!data || !oid_hex) return -1;
    
    /* For demonstration, create a simple hash */
    uint32_t hash = 0;
    for (size_t i = 0; i < size; i++) {
        hash = hash * 31 + (unsigned char)data[i];
    }
    
    /* Convert to hex string (simplified) */
    snprintf(oid_hex, 41, "%08x%08x%08x%08x%08x", 
             hash, hash ^ 0x12345678, hash ^ 0x87654321, 
             hash ^ 0xABCDEF00, hash ^ 0x00FEDCBA);
    
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_thread_compress_data(const void* input, size_t input_size,
                                 void* output, size_t* output_size) {
    /* Simplified compression (would use zlib) */
    if (!input || !output || !output_size) return -1;
    
    /* For demonstration, just copy data */
    if (*output_size < input_size) return -1;
    
    memcpy(output, input, input_size);
    *output_size = input_size;
    
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_thread_decompress_data(const void* input, size_t input_size,
                                   void* output, size_t* output_size) {
    /* Simplified decompression (would use zlib) */
    if (!input || !output || !output_size) return -1;
    
    /* For demonstration, just copy data */
    if (*output_size < input_size) return -1;
    
    memcpy(output, input, input_size);
    *output_size = input_size;
    
    return 0;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_thread_validate_oid(const char* oid_hex) {
    /* Validate OID format */
    if (!oid_hex) return 0;
    
    size_t len = strlen(oid_hex);
    if (len != 40) return 0;
    
    for (size_t i = 0; i < len; i++) {
        char c = oid_hex[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'))) {
            return 0;
        }
    }
    
    return 1;
}

/* Async API for threaded operations */

EMSCRIPTEN_KEEPALIVE
int git_wasm_async_hash_object(const char* data, size_t size, char* oid_hex, int timeout_ms) {
    git_task_t* task = submit_task(TASK_TYPE_HASH_OBJECT, (void*)data, size, oid_hex, 0);
    if (!task) return -1;
    
    return wait_for_task(task, timeout_ms);
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_async_compress_data(const void* input, size_t input_size,
                                void* output, size_t* output_size, int timeout_ms) {
    git_task_t* task = submit_task(TASK_TYPE_COMPRESS_DATA, (void*)input, input_size, 
                                  output, *output_size);
    if (!task) return -1;
    
    int result = wait_for_task(task, timeout_ms);
    if (result == 0) {
        *output_size = task->output_size;
    }
    
    return result;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_async_validate_oid(const char* oid_hex, int timeout_ms) {
    git_task_t* task = submit_task(TASK_TYPE_VALIDATE_OID, (void*)oid_hex, 0, NULL, 0);
    if (!task) return -1;
    
    return wait_for_task(task, timeout_ms);
}

/* Threading performance test */
EMSCRIPTEN_KEEPALIVE
double git_wasm_threading_benchmark(void) {
    const int num_tasks = 100;
    const size_t data_size = 1024;
    
    char test_data[data_size];
    char oid_results[num_tasks][41];
    
    /* Initialize test data */
    for (size_t i = 0; i < data_size; i++) {
        test_data[i] = (char)(i & 0xFF);
    }
    
    /* Benchmark threaded operations */
    double start_time = emscripten_get_now();
    
    /* Submit all tasks */
    git_task_t* tasks[num_tasks];
    for (int i = 0; i < num_tasks; i++) {
        tasks[i] = submit_task(TASK_TYPE_HASH_OBJECT, test_data, data_size, 
                              oid_results[i], 0);
    }
    
    /* Wait for all tasks to complete */
    int completed = 0;
    for (int i = 0; i < num_tasks; i++) {
        if (tasks[i] && wait_for_task(tasks[i], 5000) == 0) {
            completed++;
        }
    }
    
    double end_time = emscripten_get_now();
    
    if (completed == 0) return -1.0;
    
    /* Return tasks per second */
    double total_time = (end_time - start_time) / 1000.0;
    return completed / total_time;
}

/* Get threading info */
EMSCRIPTEN_KEEPALIVE
int git_wasm_get_thread_count(void) {
    return git_thread_pool.initialized ? git_thread_pool.num_threads : 0;
}

EMSCRIPTEN_KEEPALIVE
int git_wasm_get_queue_size(void) {
    return git_thread_pool.initialized ? atomic_load(&git_thread_pool.queue.count) : 0;
}

#endif /* USE_THREADS */