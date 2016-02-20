
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
    WORKER_FLAGS_NONE       = (0UL << 0),        /// The worker thread has the default behavior.
    WORKER_FLAGS_TRANSIENT  = (1UL << 0),        /// The worker thread is transient; that is, it self-terminates after a fixed interval with no work.
    WORKER_FLAGS_GENERAL    = (1UL << 1),        /// The worker thread can execute general tasks.
    WORKER_FLAGS_COMPUTE    = (1UL << 2),        /// The worker thread can execute compute tasks.
};

/// @summary Define configuration options for a thread pool.
struct WIN32_THREAD_POOL_CONFIG
{
    WIN32_TASK_SCHEDULER      *TaskScheduler;    /// The task scheduler used to launch tasks to run within the pool.
    WIN32_WORKER_ENTRYPOINT    ThreadMain;       /// The entry point of all worker threads in the pool.
    WIN32_THREAD_ARGS         *MainThreadArgs;   /// The global data managed by the main thread and available to all threads.
    size_t                     MinThreads;       /// The minimum number of active worker threads (persistent threads).
    size_t                     MaxThreads;       /// The maximum number of active worker threads in the pool.
    size_t                     WorkerArenaSize;  /// The number of bytes of thread-local memory to allocate for each active thread.
    HANDLE                     StartSignal;      /// Signal set by the coordinator to allow all active worker threads to start running.
    HANDLE                     ErrorSignal;      /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                     TerminateSignal;  /// Signal set by the coordinator to stop all active worker threads.
    uint32_t                   FlagsTransient;   /// The WORKER_FLAGS to apply to transient threads (WORKER_FLAGS_TRANSIENT is implied.)
    uint32_t                   FlagsPersistent;  /// The WORKER_FLAGS to apply to persistent threads.
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
    WIN32_TASK_SCHEDULER      *TaskScheduler;    /// The task scheduler that owns the thread pool.
    WIN32_TASK_SOURCE         *ThreadSource;     /// The WIN32_TASK_SOURCE assigned to the worker thread.
    MEMORY_ARENA              *ThreadArena;      /// The thread-local memory arena.
    WIN32_THREAD_POOL         *ThreadPool;       /// The thread pool that owns the worker thread.
    uint32_t                   PoolIndex;        /// The zero-based index of the thread in the owning thread pool.
    uint32_t                   WorkerFlags;      /// WORKER_FLAGS controlling worker thread behavior.
    WIN32_WORKER_SIGNAL        Signals;          /// The signals that can be sent to or set by a worker thread.
    WIN32_THREAD_ARGS         *MainThreadArgs;   /// The global data managed by the main thread and available to all threads.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint8_t                    Padding[20];      /// Padding out to the end of the 64-byte cacheline.
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
    uint32_t                  *WorkerFreeList;   /// The list of indices of available worker threads.
    uint32_t                   WorkerFreeCount;  /// The number of pool indices in the transient worker free list.
    WIN32_WORKER_ENTRYPOINT    WorkerMain;       /// The entry point for all threads in the pool.
    WIN32_TASK_SCHEDULER      *TaskScheduler;    /// The scheduler that owns the thread pool, or NULL.
    uint32_t                   FlagsTransient;   /// The WORKER_FLAGS to apply to transient worker threads.
    uint32_t                   FlagsPersistent;  /// The WORKER_FLAGS to apply to persistent worker threads.
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
    size_in_bytes += AllocationSizeForArray<uint32_t           >(max_threads);
    return size_in_bytes;
}

/// @summary Spawn a new worker thread within a thread pool.
/// @param thread_pool The thread pool that owns the worker thread.
/// @param pool_index The zero-based index of the worker within the thread pool.
/// @param worker_flags The WORKER_FLAGS to assign to the new worker thread.
/// @param start_data Optional data to copy to the thread, typically specifying a work item to execute.
/// @param data_size The number of bytes of data in the start_data buffer. The maximum size is 64 bytes.
/// @return Zero if the worker thread was successfully started, or -1 if an error occurred.
internal_function int
SpawnWorkerThread
(
    WIN32_THREAD_POOL *thread_pool, 
    uint32_t            pool_index,
    uint32_t          worker_flags,
    void   const       *start_data, 
    size_t const         data_size
)
{
    if (pool_index >= thread_pool->MaxThreads)
    {   // an invalid pool index was specified. this is a silent error.
        return -1;
    }
    if (data_size > 0 && task_data == NULL)
    {   // no task data was specified.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; specified %zu bytes of data but specified NULL data array.\n", data_size);
        return -1;
    }
    if (data_size > CACHELINE_SIZE)
    {   // too much task data was specified.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; specified %zu bytes of data, max is %u bytes.\n", data_size, CACHELINE_SIZE);
        return -1;
    }

    WIN32_WORKER_SIGNAL thread_signal = {};
    HANDLE              wait_ready[3] = {};
    HANDLE               worker_ready = NULL;
    HANDLE              thread_handle = NULL;
    unsigned int            thread_id = 0;

    // create a manual-reset event, signaled by the new worker, to indicate that thread initialization is complete.
    if ((worker_ready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without an event to signal, the calling thread could deadlock.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; creation of thread ready event failed (%08X).\n", GetLastError());
        return -1;
    }

    // initialize the state passed to the worker thread.
    WIN32_WORKER_THREAD *thread_state     =&thread_pool->WorkerState [pool_index];
    thread_state->TaskScheduler           = thread_pool->TaskScheduler;
    thread_state->ThreadSource            = thread_pool->WorkerSource[pool_index];
    thread_state->ThreadArena             =&thread_pool->WorkerArena [pool_index];
    thread_state->ThreadPool              = thread_pool;
    thread_state->PoolIndex               = pool_index;
    thread_state->WorkerFlags             = worker_flags;
    thread_state->Signals.ReadySignal     = worker_ready;
    thread_state->Signals.StartSignal     = thread_pool->StartSignal;
    thread_state->Signals.ErrorSignal     = thread_pool->ErrorSignal;
    thread_state->Signals.TerminateSignal = thread_pool->TerminateSignal;
    if (data_size > 0)
    {   // copy any startup data for the thread. this is typically a task to execute.
        CopyMemory(thread_state->StartData, task_data, data_size);
    }

    // spawn the worker thread. _beginthreadex ensures the CRT is properly initialized.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, thread_pool->ThreadMain, thread_state, 0, &thread_id)) == NULL)
    {   // unable to spawn the worker thread. let the caller decide if they want to terminate everybody.
        ConsoleError("ERROR: Cannot SpawnWorkerThread; thread creation failed (errno = %d).\n", errno);
        CloseHandle(worker_ready);
        return -1;
    }

    // wait for the worker thread to report that it's ready-to-run.
    wait_ready[0] = worker_ready;
    wait_ready[1] = thread_pool->ErrorSignal;
    wait_ready[2] = thread_pool->TerminateSignal;
    if (WaitForMultipleObjects(3, wait_ready, FALSE, INFINITE) == WAIT_OBJECT_0)
    {   // the worker thread reported that it's ready-to-run; we're done.
        thread_pool->OSThreadIds   [pool_index] = thread_id;
        thread_pool->OSThreadHandle[pool_index] = thread_handle;
        // TODO(rlk): Needs to be InterlockedIncrement
        thread_pool->ActiveThreads++;
    }
    else
    {   // the worker thread failed to initialize, or termination was signaled.
        CloseHandle(worker_ready);
        return -1;
    }
}

/// @summary Initialize a new thread pool. All worker threads will wait for a launch signal.
/// @param thread_pool The thread pool to initialize.
/// @param pool_config The thread pool configuration.
/// @param arena The memory arena from which to allocate global memory.
/// @return Zero if the thread pool is successfully created, or -1 if an error occurred.
public_function int
CreateThreadPool
(
    WIN32_THREAD_POOL              *thread_pool, 
    WIN32_THREAD_POOL_CONFIG const *pool_config, 
    MEMORY_ARENA                         *arena
)
{
    size_t   mem_marker = ArenaMarker(arena);
    size_t mem_required = CalculateMemoryForThreadPool(pool_config->MaxThreads);
    if (!ArenaCanAllocate(arena, mem_required, std::alignment_of<void*>::value))
    {
        ConsoleError("ERROR: Not enough free memory to initialize thread pool; need %zu bytes.\n", mem_required);
        return -1;
    }

    // initialize the thread pool state and allocate memory for variable-length arrays.
    thread_pool->MaxThreads               = pool_config->MaxThreads;
    thread_pool->ActiveThreads            = 0;
    thread_pool->WorkerArenaSize          = pool_config->WorkerArenaSize;
    thread_pool->StartSignal              = pool_config->StartSignal;
    thread_pool->ErrorSignal              = pool_config->ErrorSignal;
    thread_pool->TerminateSignal          = pool_config->TerminateSignal;
    thread_pool->MainThreadArgs           = pool_config->MainThreadArgs;
    thread_pool->OSThreadIds              = PushArray<unsigned int       >(arena, pool_config->MaxThreads);
    thread_pool->OSThreadHandle           = PushArray<HANDLE             >(arena, pool_config->MaxThreads);
    thread_pool->OSThreadArena            = PushArray<WIN32_MEMORY_ARENA >(arena, pool_config->MaxThreads);
    thread_pool->WorkerState              = PushArray<WIN32_WORKER_THREAD>(arena, pool_config->MaxThreads);
    thread_pool->WorkerSource             = PushArray<WIN32_TASK_SOURCE *>(arena, pool_config->MaxThreads);
    thread_pool->WorkerArena              = PushArray<MEMORY_ARENA       >(arena, pool_config->MaxThreads);
    thread_pool->WorkerFreeList           = PushArray<uint32_t           >(arena, pool_config->MaxThreads);
    thread_pool->WorkerFreeCount          = 0;
    thread_pool->WorkerMain               = pool_config->ThreadMain;
    thread_pool->TaskScheduler            = pool_config->TaskScheduler;
    thread_pool->FlagsTransient           = pool_config->FlagsTransient;
    thread_pool->FlagsPersistent          = pool_config->FlagsPersistent & ~WORKER_FLAGS_TRANSIENT;
    ZeroMemory(thread_pool->OSThreadIds   , pool_config->MaxThreads * sizeof(unsigned int));
    ZeroMemory(thread_pool->OSThreadHandle, pool_config->MaxThreads * sizeof(HANDLE));
    ZeroMemory(thread_pool->OSThreadArena , pool_config->MaxThreads * sizeof(WIN32_MEMORY_ARENA));
    ZeroMemory(thread_pool->WorkerState   , pool_config->MaxThreads * sizeof(WIN32_WORKER_THREAD));
    ZeroMemory(thread_pool->WorkerSource  , pool_config->MaxThreads * sizeof(WIN32_TASK_SOURCE *));
    ZeroMemory(thread_pool->WorkerArena   , pool_config->MaxThreads * sizeof(MEMORY_ARENA));

    // initialize the thread-local memory arenas.
    if (pool_config->WorkerArenaSize > 0)
    {
        for (size_t i = 0; i < pool_config->MaxThreads; ++i)
        {
            WIN32_MEMORY_ARENA *os_arena = &thread_pool->OSThreadArena[i];
            MEMORY_ARENA       *tl_arena = &thread_pool->WorkerArena[i];

            if (CreateMemoryArena(os_arena, pool_config->WorkerArenaSize, true, true) < 0)
            {   // the physical address space could not be reserved or committed.
                goto cleanup_and_fail;
            }
            if (CreateArena(tl_arena, pool_config->WorkerArenaSize, std::alignment_of<void*>::value, os_arena) < 0)
            {   // this should never happen - there's an implementation error.
                goto cleanup_and_fail;
            }
        }
    }

    // spawn workers until the minimum thread count is reached.
    for (size_t i = 0; i < pool_config->MinThreads; ++i)
    {
        WIN32_WORKER_ENTRYPOINT entry = pool_config->WorkerMain;
        WIN32_TASK_SCHEDULER   *sched = pool_config->TaskScheduler;
        WIN32_THREAD_ARGS       *args = pool_config->MainThreadArgs;
        uint32_t                flags = pool_config->FlagsPersistent;
        uint32_t                index = uint32_t(i);
        
        if (SpawnWorkerThread(thread_pool, index, flags, args, NULL, 0) < 0)
        {   // unable to spawn the worker thread; the minimum pool size cannot be met.
            goto cleanup_and_fail;
        }
    }

    // initialize the transient worker free list.
    for (size_t i = pool_config->MaxThreads; i > pool_config->MinThreads; --i)
    {
        thread_pool->WorkerFreeList[thread_pool->WorkerFreeCount++] = uint32_t(i - 1);
    }

    return 0;

cleanup_and_fail:
    if (thread_pool->ActiveThreads > 0)
    {   // signal all threads in the pool to die.
        SetEvent(pool_config->ErrorSignal);
        WaitForMultipleObjects((DWORD) thread_pool->ActiveThreads, thread_pool->OSThreadHandle, TRUE, INFINITE);
    }
    if (pool_config->WorkerArenaSize > 0 && thread_pool->OSThreadArena != NULL)
    {   // free the reserved and committed address space for thread-local arenas.
        for (size_t i = 0; i < pool_config->MaxThreads; ++i)
        {   // no cleanup needs to be performed for the 'user-facing' arena.
            DeleteMemoryArena(&thread_pool->OSThreadArena[i]);
        }
    }
    ArenaResetToMarker(mem_marker);
    return -1;
}

