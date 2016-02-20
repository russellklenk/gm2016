
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
};

/// @summary Define the signals that can be sent to or set by a worker thread.
struct WIN32_WORKER_SIGNAL
{
    HANDLE                     ReadySignal;      /// Signal set by the worker to indicate that its initialization has completed and it is ready to run.
    HANDLE                     StartSignal;      /// Signal set by the coordinator to allow the worker to start running.
    HANDLE                     ErrorSignal;      /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                     HaltSignal;       /// Signal set by the coordinator to stop all worker threads.
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
    size_in_bytes += AllocationSizeForArray<MEMORY_ARENA       >(max_threads);
    return size_in_bytes;
}

internal_function bool
SpawnWorkerThread
(
    WIN32_THREAD_POOL       *thread_pool, 
    WIN32_WORKER_ENTRYPOINT  thread_main, 
    uint32_t                  pool_index,
    uint32_t                worker_flags,
    WIN32_TASK_SCHEDULER      *scheduler, 
    WIN32_THREAD_ARGS         *main_args, 
)
{
}

