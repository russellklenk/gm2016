
/// @summary Define the function signature for a worker thread entry point.
/// @param argp Pointer to the WIN32_WORKER_THREAD containing all of the state data for the worker.
/// @return The thread exit code.
typedef unsigned int (__cdecl *WIN32_WORKER_ENTRYPOINT)
(
    void *argp
);

/// @summary Define bitflags controlling worker thread behavior.
enum WORKER_FLAGS : uint32_t
{
    WORKER_FLAGS_NONE       = (0UL << 0),     /// The worker thread has the default behavior.
    WORKER_FLAGS_TRANSIENT  = (1UL << 0),     /// The worker thread is transient; that is, it self-terminates after a fixed interval with no work.
    WORKER_FLAGS_GENERAL    = (1UL << 1),     /// The worker thread can execute general tasks.
    WORKER_FLAGS_COMPUTE    = (1UL << 2),     /// The worker thread can execute compute tasks.
};

/// @summary Define the signals that can be sent to or set by a worker thread.
struct WIN32_WORKER_SIGNAL
{
    HANDLE                     ReadySignal;      /// Signal set by the worker to indicate that its initialization has completed and it is ready to run.
    HANDLE                     StartSignal;      /// Signal set by the coordinator to allow the worker to start running.
    HANDLE                     ErrorSignal;      /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                     TerminateSignal;  /// Signal set by the coordinator to stop all worker threads.
};

/// @summary Define the state data associated with a single worker in a thread pool.
struct WIN32_WORKER_THREAD
{
    WIN32_TASK_SOURCE         *ThreadSource;     /// The WIN32_TASK_SOURCE assigned to the worker thread.
    MEMORY_ARENA              *ThreadArena;      /// The thread-local memory arena.
    WIN32_THREAD_POOL         *ThreadPool;       /// The thread pool that owns the worker thread.
    uint32_t                   PoolIndex;        /// The zero-based index of the thread in the owning thread pool.
    uint32_t                   Flags;            /// WORKER_FLAGS controlling worker thread behavior.
    WIN32_WORKER_SIGNAL        Signals;          /// The signals that can be sent to or set by a worker thread.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint8_t                    Padding[28];      /// Padding out to the end of the 64-byte cacheline.
#endif
    uint8_t                    StartData[64];    /// Additional data to be passed to the thread at startup, for example, a task to execute.
};

/// @summary Define the state data associated with a pool of threads.
struct WIN32_THREAD_POOL
{
    size_t                     MaxThreads;       /// The maximum number of threads in the pool.
    size_t                     ActiveThreads;    /// The number of active threads in the pool. 
    size_t                     WorkerArenaSize;  /// The size of each thread-local memory arena, in bytes.
    HANDLE                     StartSignal;      /// Manual-reset event signaled by the coordinator to allow the worker to start running.
    HANDLE                     ErrorSignal;      /// Manual-reset event signaled by any thread to indicate that a fatal error has occurred.
    HANDLE                     TerminateSignal;  /// Manual-reset event signaled by the coordinator to terminate all worker threads.
    WIN32_THREAD_ARGS         *MainThreadArgs;   /// The global data managed by the main thread and available to all threads.
    unsigned int              *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE                    *OSThreadHandle;   /// The operating system thread handle for each worker thread.
    WIN32_MEMORY_ARENA        *OSThreadArena;    /// The underlying OS memory arena for each worker thread.
    WIN32_WORKER_THREAD       *WorkerState;      /// The state data for each worker thread.
    WIN32_TASK_SOURCE        **WorkerSource;     /// The TASK_SOURCE assigned to each worker thread.
    MEMORY_ARENA              *WorkerArena;      /// The thread-local memory arena assigned to each worker thread.
    WIN32_WORKER_ENTRYPOINT    WorkerMain;       /// The entry point for all threads in the pool.
    WIN32_TASK_SCHEDULER      *Scheduler;        /// The scheduler that owns the thread pool, or NULL.
};

/// @summary Calculate the amount of memory required for a thread pool, not including the per-thread memory arena.
/// @param max_threads The maximum number of threads in the pool.
/// @return The number of bytes required for thread pool initialization, not including the size of the WIN32_THREAD_POOL instance.
internal_function size_t
CalculateMemoryForThreadPool
(
    size_t max_threads
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += AllocationSizeForArray<unsigned int       >(max_threads);
    size_in_bytes += AllocationSizeForArray<HANDLE             >(max_threads);
    size_in_bytes += AllocationSizeForArray<WIN32_MEMORY_ARENA >(max_threads);
    size_in_bytes += AllocationSizeForArray<WIN32_WORKER_THREAD>(max_threads);
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_SOURCE* >(max_threads);
    size_in_bytes += AllocationSizeForArray<MEMORY_ARENA       >(max_threads);
    return size_in_bytes;
}

internal_function int
SpawnWorkerThread
(
    WIN32_THREAD_POOL       *thread_pool, 
    WIN32_WORKER_ENTRYPOINT  thread_main, 
    uint32_t                  pool_index,
    uint32_t                worker_flags,
    WIN32_TASK_SCHEDULER      *scheduler, 
    WIN32_THREAD_ARGS         *main_args, 
    void   const             *start_data, 
    size_t const               data_size
)
{
    if (pool_index >= thread_pool->MaxThreads)
    {   // an invalid pool index was specified. this is a silent error.
        return false;
    }
    if (data_size > 0 && task_data == NULL)
    {   // no task data was specified.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; specified %zu bytes of data but specified NULL data array.\n", data_size);
        return false;
    }
    if (data_size > CACHELINE_SIZE)
    {   // too much task data was specified.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; specified %zu bytes of data, max is %u bytes.\n", data_size, CACHELINE_SIZE);
        return false;
    }

    MEMORY_ARENA        *thread_arena =&thread_pool->WorkerArena [pool_index];
    WIN32_WORKER_THREAD *thread_state =&thread_pool->WorkerState [pool_index];
    WIN32_TASK_SOURCE   *thread_tasks = thread_pool->WorkerSource[pool_index];
    WIN32_WORKER_SIGNAL thread_signal = {};
    HANDLE              wait_ready[3] = {};
    HANDLE               worker_ready = NULL;
    HANDLE              thread_handle = NULL;
    unsigned int            thread_id = 0;

    // create a manual-reset event, signaled by the new worker, to indicate that thread initialization is complete.
    if ((worker_ready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {
        ConsoleError("ERROR: Cannot SpawnWorkerThread; creation of thread ready event failed (%08X).\n", GetLastError());
        return false;
    }

    // initialize the state passed to the worker thread.
    thread_state->ThreadSource            = thread_pool->WorkerSource[pool_index];
    thread_state->ThreadArena             =&thread_pool->WorkerArena [pool_index];
    thread_state->ThreadPool              = thread_pool;
    thread_state->PoolIndex               = pool_index;
    thread_state->Flags                   = worker_flags;
    thread_state->Signals.ReadySignal     = worker_ready;
    thread_state->Signals.StartSignal     = thread_pool->StartSignal;
    thread_state->Signals.ErrorSignal     = thread_pool->ErrorSignal;
    thread_state->Signals.TerminateSignal = thread_pool->TerminateSignal;
    if (data_size > 0)
    {   // copy any startup data for the thread. this is typically a task to execute.
        CopyMemory(thread_state->StartData, task_data, data_size);
    }

    // spawn the worker thread. _beginthreadex ensures the CRT is properly initialized.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, thread_main, thread_state, 0, &thread_id)) == NULL)
    {   // unable to spawn the worker thread. let the caller decide if they want to terminate everybody.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; thread creation failed (errno = %d).\n", errno);
        CloseHandle(worker_ready);
        return false;
    }

    // wait for the worker thread to report that it's ready-to-run.
    wait_ready[0] = worker_ready;
    wait_ready[1] = thread_pool->ErrorSignal;
    wait_ready[2] = thread_pool->TerminateSignal;
    if (WaitForMultipleObjects(3, wait_ready, FALSE, INFINITE) == WAIT_OBJECT_0)
    {   // the worker thread reported that it's ready-to-run; we're done.
    }
    else
    {   // the worker thread failed to initialize, or termination was signaled.
        CloseHandle(worker_ready);
        return false;
    }
}

