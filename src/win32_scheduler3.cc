/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement a low-overhead task scheduler to distribute tasks on a
/// thread pool. The system supports two main categories of tasks:
/// * compute tasks are non-blocking and CPU heavy,
/// * general tasks may block and are generally not CPU heavy.
/// Tasks may spawn one or more other tasks, and support dependencies such that
/// a task may wait for one or more tasks to complete before it will launch.
/// Each type of task executes in a separate thread pool.
/// Windows I/O completion ports are used to wake threads in the pool.
/// The task scheduler system supports built-in profiling using Event Tracing
/// for Windows, in conjunction with custom information, at the cost of some 
/// fixed space and time overhead (you lose a thread in the compute pool.)
///////////////////////////////////////////////////////////////////////////80*/

// TODO(rlk): Each pool has a separate IOCP so that threads in that pool can be woken separately.
// When a task is definied, it signals the IOCP corresponding to its pool.
// TODO(rlk): In this revision, switch to using the same data for both types of tasks. This is 
// possible because we'll switch to hunting for a free task in the TASK_SOURCE. This way, both
// types of tasks support the same features and have the same data; they differ only in which 
// pool they execute on.
// TODO(rlk): Add explicit support for a 'batch push' mode, where new tasks are pushed to the 
// TASK_SOURCE, but no signals are sent until all tasks have been defined.
// TODO(rlk): Each TASK_SOURCE now has two dequeues, one for the compute pool and one for the 
// general pool. The MPMC general task queue is no longer needed.
// TODO(rlk): Profiling can be completely stripped out using a #define.
// TODO(rlk): Instead of using two bits for buffer index, use those bits to represent task size.
// The task size information can be used by a worker thread to determine how many tasks to steal
// when the thread is woken; for example, a worker may wake and be able to steal 16 points worth
// of work up-front to reduce queue contention. Perhaps it's worth it to steal N tasks, and if 
// the worker actually filled its workload, post a notification to wake another thread?

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Define the mask and shift values for the constituient parts of a compute task ID.
/// The task ID layout is as follows:
/// 31.............................0
/// VWWWWWWWWWWWWIIIIIIIIIIIIIIIIPSS
/// 
/// Where the letters mean the following:
/// V: Set if the ID is valid, clear if the ID is not valid.
/// W: The zero-based index of the task source. The system supports up to 4095 sources, plus 1 to represent the thread that submits the root tasks.
/// I: The zero-based index of the task data within the worker thread's task buffer.
/// P: Set if the task executes on the general pool, clear if the task executes on the compute pool.
/// S: The size of the task, representing how much compute work it has to perform.
/// 
/// The task ID serves as a pointer that can be used to address a specific task in the TASK_SOURCE that created it.
#ifndef TASK_ID_LAYOUT_DEFINED
#define TASK_ID_LAYOUT_DEFINED
#define TASK_ID_MASK_SIZE_P               (0x00000003UL)
#define TASK_ID_MASK_SIZE_U               (0x00000003UL)
#define TASK_ID_MASK_POOL_P               (0x00000004UL)
#define TASK_ID_MASK_POOL_U               (0x00000001UL)
#define TASK_ID_MASK_INDEX_P              (0x0007FFF8UL)
#define TASK_ID_MASK_INDEX_U              (0x0000FFFFUL)
#define TASK_ID_MASK_SOURCE_P             (0x7FF80000UL)
#define TASK_ID_MASK_SOURCE_U             (0x00000FFFUL)
#define TASK_ID_MASK_VALID_P              (0x80000000UL)
#define TASK_ID_MASK_VALID_U              (0x00000001UL)
#define TASK_ID_SHIFT_SIZE                (0)
#define TASK_ID_SHIFT_POOL                (2)
#define TASK_ID_SHIFT_INDEX               (3)
#define TASK_ID_SHIFT_SOURCE              (19)
#define TASK_ID_SHIFT_VALID               (31)
#endif

/// @summary Define the maximum number of task sources supported by the scheduler.
#ifndef MAX_TASK_SOURCES
#define MAX_TASK_SOURCES                  (4096)
#endif

/// @summary Define the maximum number of tasks that can be stored in a single task source. The runtime limit may be lower.
#ifndef MAX_TASKS_PER_SOURCE
#define MAX_TASKS_PER_SOURCE              (65536)
#endif

/// @summary Define the identifier returned to represent an invalid task ID.
#ifndef INVALID_TASK_ID
#define INVALID_TASK_ID                   ((task_id_t) 0x7FFFFFFFUL)
#endif

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/
struct WIN32_THREAD_ARGS;
struct WIN32_TASK_SCHEDULER;
struct WIN32_TASK_SCHEDULER_CONFIG;
struct WIN32_WORKER_THREAD;
struct WIN32_THREAD_POOL;
struct WIN32_THREAD_POOL_CONFIG;
struct TASK_SOURCE;
struct TASK_QUEUE;
struct TASK_DATA;
struct PERMITS_LIST;

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Use a unique 32-bit integer to identify a task.
typedef uint32_t task_id_t;

/// @summary Define the function signature for a worker thread entry point.
/// @param argp Pointer to the WIN32_WORKER_THREAD containing all of the state data for the worker.
/// @return The thread exit code.
typedef unsigned int (__cdecl *WIN32_WORKER_ENTRYPOINT)
(
    void *argp
);

/// @summary Define the function signature for a task entry point.
/// @param task_id The identifier of the task being executed.
/// @param thread_source A WIN32_TASK_SOURCE associated with the thread that can be used to launch sub-tasks.
/// @param work_item A thread-local copy of the data associated with the task to execute.
/// @param thread_arena A thread-local memory arena that can be used to allocate working memory. The arena is reset after the entry point returns.
/// @param main_args Global data managed by the main application thread.
/// @param scheduler The scheduler that owns the task being executed.
typedef void (*TASK_ENTRYPOINT)
(
    task_id_t                   task_id, 
    TASK_SOURCE          *thread_source, 
    TASK_DATA                *work_item, 
    MEMORY_ARENA          *thread_arena, 
    WIN32_THREAD_ARGS        *main_args, 
    WIN32_TASK_SCHEDULER     *scheduler
);

/// @summary Define bitflags controlling worker thread behavior.
enum WORKER_FLAGS : uint32_t
{
    WORKER_FLAGS_NONE        = (0UL << 0),       /// The worker thread has the default behavior.
};

/// @summary Define identifiers for task ID validity. An ID can only be valid or invalid.
enum TASK_ID_TYPE : uint32_t
{
    TASK_ID_TYPE_INVALID     = 0,                /// The task identifier specifies an invalid task.
    TASK_ID_TYPE_VALID       = 1,                /// The task identifier specifies a valid task.
};

/// @summary Define identifiers for supported task thread pools.
enum TASK_POOL : uint32_t
{
    TASK_POOL_COMPUTE        = 0,                /// The task executes on the compute thread pool, designed for non-blocking, work-heavy tasks.
    TASK_POOL_GENERAL        = 1,                /// The task executes on the general thread pool, designed for blocking, light-CPU tasks.
    TASK_POOL_COUNT          = 2                 /// The number of thread pools defined by the scheduler.
};

/// @summary Define identifiers for the supported task workload sizes.
enum TASK_SIZE : uint32_t
{
    TASK_SIZE_SMALL          = 0,                /// The task represents a minimal compute workload.
    TASK_SIZE_MEDIUM         = 1,                /// The task represents a non-trivial compute workload.
    TASK_SIZE_LARGE          = 2,                /// The task represents a significant compute workload.
};

/// @summary Define a structure specifying the constituent parts of a task ID.
struct TASK_ID_PARTS
{
    uint32_t                 ValidTask;          /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t                 PoolType;           /// One of TASK_POOL specifying the thread pool that executes the task.
    uint32_t                 TaskSize;           /// One of TASK_SIZE specifying the relative compute workload of the task.
    uint32_t                 SourceIndex;        /// The zero-based index of the thread that defines the task.
    uint32_t                 TaskIndex;          /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define the public data associated with a task. User-supplied data is stored inline.
#pragma warning (push)
#pragma warning (disable:4324)                   /// Structure was padded due to __declspec(align())
struct cacheline_align       TASK_DATA
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      MAX_DATA              = 48;
    std::atomic<int32_t>     Permits;            /// The number of permits that must be satisfied before this task can run.
    task_id_t                ParentTask;         /// The identifier of the parent task, or INVALID_TASK_ID.
    TASK_ENTRYPOINT          TaskMain;           /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t                 Reserved1;          /// Padding; unused.
#endif
    uint8_t                  Data[MAX_DATA];     /// User-supplied argument data associated with the work item.
};
#pragma warning (pop)                            /// Structure was padded due to __declspec(align())

/// @summary Define the data representing a deque of task identifiers. Each TASK_SOURCE has one TASK_QUEUE per-thread pool.
/// The owning thread can perform push and take operations. Other threads can perform concurrent steal operations.
#pragma warning (push)
#pragma warning (disable:4324)                   /// Structure was padded due to __declspec(align())
struct cacheline_align       TASK_QUEUE
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      PAD                   = CACHELINE_SIZE - sizeof(std::atomic<int64_t>);
    std::atomic<int64_t>     Public;             /// The public end of the deque, updated by steal operations (Top).
    uint8_t                  Pad0[PAD];          /// Padding separating the public data from the private data.
    std::atomic<int64_t>     Private;            /// The private end of the deque, updated by push and take operations (Bottom).
    uint8_t                  Pad1[PAD];          /// Padding separating the private data from the storage data.
    int64_t                  Mask;               /// The bitmask used to map the Top and Bottom indices into the storage array.
    task_id_t               *Tasks;              /// The identifiers for the tasks in the queue.
};
#pragma warning (pop)                            /// Structure was padded due to __declspec(align())

/// @summary Defines the data associated with a set of tasks waiting on another task to complete.
#pragma warning (push)
#pragma warning (disable:4324)                   /// Structure was padded due to __declspec(align())
struct cacheline_align       PERMITS_LIST
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      MAX_TASKS             = 15;
    std::atomic<int32_t>     Count;              /// The number of items in the permits list.
    task_id_t                Tasks[MAX_TASKS];   /// The task IDs in the permits list. This is the set of tasks to launch when the owning task completes.
};
#pragma warning (pop)                            /// Structure was padded due to __declspec(align())

/// @summary Define the data associated with a thread that can produce compute tasks (but not necessarily execute them.)
/// Each worker thread in the scheduler thread pool is a COMPUTE_TASK_SOURCE that can also execute tasks.
/// The maximum number of task sources is fixed at scheduler creation time.
#pragma warning (push)
#pragma warning (disable:4324)                   /// Structure was padded due to __declspec(align())
struct cacheline_align       TASK_SOURCE
{
    TASK_QUEUE               ComputeWorkQueue;   /// The queue of ready-to-run task IDs to execute on the compute pool.
    TASK_QUEUE               GeneralWorkQueue;   /// The queue of ready-to-run task IDs to execute on the general pool.
    HANDLE                   ComputePoolPort;    /// The I/O completion port used to wake threads in the compute pool.
    HANDLE                   GeneralPoolPort;    /// The I/O completion port used to wake threads in the general pool.

    uint32_t                 SourceIndex;        /// The zero-based index of this TASK_SOURCE in the scheduler source list. Constant.
    uint32_t                 SourceCount;        /// The total number of task sources defined in the scheduler. Constant.
    TASK_SOURCE             *TaskSources;        /// The list of per-source state for each task source. Managed by the scheduler.
    
    uint32_t                 MaxTasks;           /// The allocation capacity of the task buffers.
    uint32_t                 TaskIndex;          /// The zero-based index of the next task to allocate.
    TASK_DATA               *WorkItems;          /// The work item definition for each task.
    int32_t                 *WorkCount;          /// The outstanding work counter for each task.
    PERMITS_LIST            *PermitList;         /// The list of tasks that become permitted to run for each task.
};
#pragma warning (pop)                            /// Structure was padded due to __declspec(align())

/// @summary Define the state data associated with a single worker in a thread pool.
struct WIN32_WORKER_THREAD
{
    WIN32_THREAD_POOL       *ThreadPool;         /// The thread pool that owns the worker thread.
    MEMORY_ARENA            *ThreadArena;        /// The thread-local memory arena.
    TASK_SOURCE             *ThreadSource;       /// The TASK_SOURCE allocated to the worker thread.
    HANDLE                   ReadySignal;        /// A manual-reset event, created by the pool and signaled by the worker when the worker becomes ready-to-run.
    std::atomic<int32_t>    *TerminateFlag;      /// An atomic integer that is set to non-zero when the worker thread should terminate.
    size_t                   PoolIndex;          /// The zero-based index of the thread in the owning thread pool.
    uint32_t                 WorkerFlags;        /// WORKER_FLAGS controlling worker thread behavior.
};

/// @summary Define the state data associated with a pool of threads.
struct WIN32_THREAD_POOL
{
    size_t                   MaxThreads;         /// The maximum number of threads in the pool.
    size_t                   ActiveThreads;      /// The number of active threads in the pool. 
    size_t                   WorkerArenaSize;    /// The size of each thread-local memory arena, in bytes.
    HANDLE                   CompletionPort;     /// The I/O completion port used to wait and wake threads in the pool.
    HANDLE                   LaunchSignal;       /// Manual-reset event signaled by the coordinator to allow the worker to start running.
    WIN32_THREAD_ARGS       *MainThreadArgs;     /// The global data managed by the main thread and available to all threads.
    unsigned int            *OSThreadIds;        /// The operating system identifiers for each worker thread.
    HANDLE                  *OSThreadHandle;     /// The operating system thread handle for each worker thread.
    WIN32_MEMORY_ARENA      *OSThreadArena;      /// The underlying OS memory arena for each worker thread.
    WIN32_WORKER_THREAD     *WorkerState;        /// The state data for each worker thread.
    TASK_SOURCE            **WorkerSource;       /// The TASK_SOURCE assigned to each worker thread.
    MEMORY_ARENA            *WorkerArena;        /// The thread-local memory arena assigned to each worker thread.
    WIN32_WORKER_ENTRYPOINT  WorkerMain;         /// The entry point for all threads in the pool.
    WIN32_TASK_SCHEDULER    *TaskScheduler;      /// The scheduler that owns the thread pool, or NULL.
    std::atomic<int32_t>     TerminateFlag;      /// An integer value to be set to non-zero when worker threads should terminate.
    uint32_t                 WorkerFlags;        /// The WORKER_FLAGS to apply to worker threads in the pool.
};

/// @summary Define configuration options for a thread pool.
struct WIN32_THREAD_POOL_CONFIG
{
    WIN32_TASK_SCHEDULER    *TaskScheduler;      /// The task scheduler used to launch tasks to run within the pool.
    WIN32_WORKER_ENTRYPOINT  ThreadMain;         /// The entry point of all worker threads in the pool.
    WIN32_THREAD_ARGS       *MainThreadArgs;     /// The global data managed by the main thread and available to all threads.
    size_t                   MinThreads;         /// The minimum number of active worker threads (persistent threads).
    size_t                   MaxThreads;         /// The maximum number of active worker threads in the pool.
    size_t                   WorkerArenaSize;    /// The number of bytes of thread-local memory to allocate for each active thread.
    size_t                   WorkerSourceIndex;  /// The zero-based index of the first WIN32_TASK_SOURCE in the scheduler allocated to the pool.
    HANDLE                   LaunchSignal;       /// Signal set by the coordinator to allow all active worker threads to start running.
    uint32_t                 WorkerFlags;        /// The WORKER_FLAGS to apply to worker threads in the pool.
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Spawn a new worker thread within a thread pool.
/// @param thread_pool The thread pool that owns the worker thread.
/// @param pool_index The zero-based index of the worker within the thread pool.
/// @param worker_flags The WORKER_FLAGS to assign to the new worker thread.
/// @return Zero if the worker thread was successfully started, or -1 if an error occurred.
internal_function int
SpawnWorkerThread
(
    WIN32_THREAD_POOL *thread_pool, 
    size_t              pool_index,
    uint32_t          worker_flags
)
{
    if (pool_index >= thread_pool->MaxThreads)
    {   // an invalid pool index was specified. this is a silent error.
        return -1;
    }

    HANDLE        worker_ready = NULL;
    HANDLE       thread_handle = NULL;
    unsigned int     thread_id = 0;

    // create a manual-reset event, signaled by the new worker, to indicate that thread initialization is complete.
    if ((worker_ready = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // without an event to signal, the calling thread could deadlock.
        ConsoleError("ERROR (%S): Creation of thread ready event failed (%08X).\n", __FUNCTION__, GetLastError());
        return -1;
    }

    // initialize the state passed to the worker thread.
    WIN32_WORKER_THREAD   *thread = &thread_pool->WorkerState[pool_index];
    thread->ThreadPool   = thread_pool;
    thread->ThreadArena  =&thread_pool->WorkerArena [pool_index];
    thread->ThreadSource = thread_pool->WorkerSource[pool_index];
    thread->ReadySignal  = worker_ready;
    thread->PoolIndex    = pool_index;
    thread->WorkerFlags  = worker_flags;

    // spawn the worker thread. _beginthreadex ensures the CRT is properly initialized.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, thread_pool->WorkerMain, thread_state, 0, &thread_id)) == NULL)
    {   // unable to spawn the worker thread. let the caller decide if they want to terminate everybody.
        ConsoleError("ERROR (%S): Thread creation failed (errno = %d).\n", __FUNCTION__, errno);
        CloseHandle(worker_ready);
        return -1;
    }

    // wait for the worker thread to report that it's ready-to-run.
    if (WaitForSingleObject(worker_ready, INFINITE) == WAIT_OBJECT_0)
    {   // the worker thread reported that it's ready-to-run; we're done.
        thread_pool->OSThreadIds   [pool_index] = thread_id;
        thread_pool->OSThreadHandle[pool_index] = thread_handle;
        thread_pool->ActiveThreads++;
        return 0;
    }
    else
    {   // the worker thread failed to initialize, or termination was signaled.
        ConsoleError("ERROR (%S): Worker thread failed to initialize (%08X).\n", __FUNCTION__, GetLastError());
        CloseHandle(worker_ready);
        return -1;
    }
}

/// @summary Perform any top-level cleanup for a single thread in a thread pool. The calling thread is blocked until the worker terminates.
/// @param thread_pool The thread pool that owns the worker thread.
/// @param pool_index The zero-based index of the worker thread within the pool.
internal_function void
CleanupWorkerThread
(
    WIN32_THREAD_POOL *thread_pool, 
    size_t              pool_index
)
{
    WIN32_WORKER_THREAD *state = &thread_pool->WorkerState[pool_index];
    HANDLE thread = thread_pool->OSThreadHandle[pool_index];
    if (thread != NULL)
    {   // wait for the thread to terminate before deleting any thread-local resources.
        WaitForSingleObject(thread, INFINITE);
        if (state->ReadySignal != NULL)
        {   // close the manual-reset event created by the pool for the thread.
            CloseHandle(state->ReadySignal);
            state->ReadySignal = NULL;
        }
    }
}

/// @summary Used by a worker thread to wait for work to be posted to its thread pool.
/// @param thread_pool The thread pool that owns the worker thread entering the wait state.
/// @param worker_index The zero-based index of the worker thread within its owning pool.
/// @return The TASK_SOURCE that caused the wake, which may be NULL for general notifications.
internal_function TASK_SOURCE*
WorkerThreadWaitForWakeup
(
    WIN32_THREAD_POOL *thread_pool, 
    size_t            worker_index
)
{
    OVERLAPPED *ov_addr     = NULL;
    ULONG_PTR   source_addr = 0;
    DWORD       num_bytes   = 0;

    if (GetQueuedCompletionStatus(thread_pool->CompletionPort, &num_bytes, &source_addr, &ov_addr, INFINITE))
    {   // the completion key (source_addr) is the TASK_SOURCE that has work available.
        // it may be NULL if this is just a general wakeup (to check status, etc.)
        return (TASK_SOURCE*) source_addr;
    }
    else
    {
        ConsoleError("ERROR (%S): Wait-for-wakeup failed for worker %zu (%08X).\n", __FUNCTION__, worker_index, GetLastError());
        return NULL;
    }
}

/// @summary Wake one or more worker threads to process work items or receive a notification.
/// @param completion_port The I/O completion port associated with the thread pool to notify.
/// @param worker_source The TASK_SOURCE of the thread that has work available, or NULL for general notifications.
/// @param thread_count The number of threads to wake up.
internal_function void
WakeWorkerThreads
(
    HANDLE      completion_port, 
    TASK_SOURCE  *worker_source,
    size_t         thread_count
)
{
    DWORD error   = ERROR_SUCCESS;
    for (size_t i = 0; i < thread_count; ++i)
    {
        if (!PostQueuedCompletionStatus(completion_port, 0, (ULONG_PTR) worker_source, NULL))
        {   // only report the first failure.
            if (error == ERROR_SUCCESS)
            {   // save the error code.
                error = GetLastError();
            } break;
        }
    }
    if (error != ERROR_SUCCESS)
    {
        ConsoleError("ERROR (%S): At least one wakeup notification failed (%08X).\n", __FUNCTION__, error);
    }
}

/// @summary Wake up all worker threads in a thread pool.
/// @param thread_pool The thread pool to wake.
internal_function inline void
WakeAllWorkerThreads
(
    WIN32_THREAD_POOL *thread_pool
)
{
    WakeWorkerThreads(thread_pool->CompletionPort, NULL, thread_pool->MaxThreads);
}

/// @summary Wake up worker threads to process compute jobs.
/// @param source The TASK_SOURCE of the thread that has available work in its compute queue.
/// @param wake_count The number of worker threads to wake.
internal_function inline void
WakeComputePoolWorkers
(
    TASK_SOURCE    *source, 
    size_t      wake_count
)
{
    WakeWorkerThreads(source->ComputePoolPort, source, wake_count);
}

/// @summary Wake up worker threads to process general asynchronous jobs.
/// @param source The TASK_SOURCE of the thread that has available work in its general queue.
/// @param wake_count The number of worker threads to wake.
internal_function inline void
WakeGeneralPoolWorkers
(
    TASK_SOURCE    *source,
    size_t      wake_count
)
{
    WakeWorkerThreads(source->GeneralPoolPort, source, wake_count);
}

/// @summary Create a task ID from its constituient parts.
/// @param task_size One of TASK_SIZE specifying the CPU workload of the task.
/// @param pool_type One of TASK_POOL specifying the thread pool that will run the task.
/// @param task_index The zero-based index of the task within the task buffer of the TASK_SOURCE that stores the task definition.
/// @param source_index The zero-based index of the TASK_SOURCE that stores the task definition.
/// @param task_id_type One of TASK_ID_TYPE indicating whether the task ID is valid.
/// @return The task identifier.
internal_function inline task_id_t
MakeTaskId
(
    uint32_t      task_size, 
    uint32_t      pool_type, 
    uint32_t     task_index, 
    uint32_t   source_index, 
    uint32_t   task_id_type = TASK_ID_TYPE_VALID
)
{
    return ((task_size    & TASK_ID_MASK_SIZE_U  ) << TASK_ID_SHIFT_SIZE  ) | 
           ((pool_type    & TASK_ID_MASK_POOL_U  ) << TASK_ID_SHIFT_POOL  ) | 
           ((task_index   & TASK_ID_MASK_INDEX_U ) << TASK_ID_SHIFT_INDEX ) |
           ((source_index & TASK_ID_MASK_SOURCE_U) << TASK_ID_SHIFT_SOURCE) | 
           ((task_id_type & TASK_ID_MASK_VALID_U ) << TASK_ID_SHIFT_VALID );
}

/// @summary Determine whether an ID identifies a valid task.
/// @param id The task identifier.
/// @return true if the identifier specifies a valid task.
internal_function inline bool
IsValidTask
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0);
}

/// @summary Determine whether an ID identifies a valid task that will execute in the general thread pool.
/// @param id The task identifier to parse.
/// @return true if the task identifier specifies a valid task that will execute in the general thread pool.
internal_function inline bool 
RunsOnGeneralPool
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0) && 
           (((id & TASK_ID_MASK_POOL_P ) >> TASK_ID_SHIFT_POOL ) == TASK_POOL_GENERAL);
}

/// @summary Determine whether an ID identifies a valid task that will execute in the compute thread pool.
/// @param id The task identifier to parse.
/// @return true if the task identifier specifies a valid task that will execute in the compute thread pool.
internal_function inline bool
RunsOnComputePool
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0) && 
           (((id & TASK_ID_MASK_POOL_P ) >> TASK_ID_SHIFT_POOL ) == TASK_POOL_COMPUTE);
}

/// @summary Retrieve the TASK_SIZE from a task ID.
/// @param id The task identifier to parse.
/// @return The TASK_SIZE value representing the relative compute workload of the task.
internal_function inline uint32_t
GetTaskWorkSize
(
    task_id_t id
)
{
    return ((id & TASK_ID_MASK_SIZE_P) >> TASK_ID_SHIFT_SIZE);
}

/// @summary Retrieve the work item data for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or TASK_SOURCE::TaskSources.
/// @return A pointer to the work item data.
internal_function inline TASK_DATA*
GetTaskWorkItem
(
    task_id_t           task,
    TASK_SOURCE *source_list
)
{
    uint32_t const source_index = (task & TASK_ID_MASK_SOURCE_P) >> TASK_ID_SHIFT_SOURCE;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[source_index].WorkItems[task_index];
}

/// @summary Retrieve the work counter for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or TASK_SOURCE::TaskSources.
/// @return A pointer to the work counter associated with the task.
internal_function inline int32_t*
GetTaskWorkCount
(
    task_id_t           task,
    TASK_SOURCE *source_list
)
{
    uint32_t const source_index = (task & TASK_ID_MASK_SOURCE_P) >> TASK_ID_SHIFT_SOURCE;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[source_index].WorkCounts[task_index];
}

/// @summary Retrieve the list of permits for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or TASK_SOURCE::TaskSources.
/// @return A pointer to the permits list associated with the task.
internal_function inline PERMITS_LIST*
GetTaskPermitsList
(
    task_id_t           task, 
    TASK_SOURCE *source_list
)
{
    uint32_t const source_index = (task & TASK_ID_MASK_SOURCE_P) >> TASK_ID_SHIFT_SOURCE;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[source_index].PermitList[task_index];
}

/// @summary Push an item onto the private end of a task queue. This function can only be called by the thread that owns the queue, and may execute concurrently with one or more steal operations.
/// @param queue The queue to receive the item.
/// @param task The identifier of the task that is ready to run.
/// @return true if the task was written to the queue.
internal_function inline bool
TaskQueuePush
(
    TASK_QUEUE *queue, 
    task_id_t    task
)
{
    int64_t b = queue->Private.load(std::memory_order_relaxed);      // atomically load the private end of the queue. only Push and Take may modify the private end.
    queue->Tasks[b & queue->Mask] = task;                            // store the new item at the end of the Tasks array.
    std::atomic_thread_fence(std::memory_order_release);             // ensure that the task ID is written to the Tasks array.
    queue->Private.store(b+1,std::memory_order_relaxed);             // make the new item visible to a concurrent steal/subsequent take operation (push to private end.)
    return true;
}

/// @summary Take an item from the private end of a task queue. This function can only be called by the thread that owns the queue, and may execute concurrently with one or more steal operations.
/// @param queue The queue from which the item will be removed.
/// @return The task identifier, or INVALID_TASK_ID if the queue is empty.
internal_function task_id_t
TaskQueueTake
(
    TASK_QUEUE *queue
)
{
    int64_t b = queue->Private.load(std::memory_order_relaxed) - 1; // safe since no concurrent Push operation is allowed.
    queue->Private.store(b , std::memory_order_relaxed);            // complete the 'pop' from the private end (LIFO).
    std::atomic_thread_fence(std::memory_order_seq_cst);            // make the 'pop' visible to a concurrent steal.
    int64_t t = queue->Public.load(std::memory_order_relaxed);

    if (t <= b)
    {   // the task queue is non-empty.
        task_id_t task = queue->Tasks[b & queue->Mask];
        if (t != b)
        {   // there's at least one more item in the queue; no need to race.
            return task;
        }
        // this was the last item in the queue. race to claim it.
        if (!queue->Public.compare_exchange_strong(t, t+1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // this thread lost the race.
            task = INVALID_TASK_ID;
        }
        queue->Private.store(t + 1, std::memory_order_relaxed);
        return task;
    }
    else
    {   // the queue is currently empty.
        queue->Private.store(t, std::memory_order_relaxed);
        return INVALID_TASK_ID;
    }
}

/// @summary Attempt to steal an item from the public end of the queue. This function can be called by any thread EXCEPT the thread that owns the queue, and may execute concurrently with a push or take operation, and one or more steal operations.
/// @param queue The queue from which the item will be removed.
/// @return The task identifier, or INVALID_TASK_ID if the queue is empty or the calling thread lost the race for the last item.
internal_function task_id_t
TaskQueueSteal
(
    TASK_QUEUE *queue
)
{
    int64_t t = queue->Public.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t b = queue->Private.load(std::memory_order_acquire);

    if (t < b)
    {   // the task queue is non-empty. save the task ID.
        task_id_t task = queue->Tasks[t & queue->Mask];
        // race with other threads to claim the item.
        if (queue->Public.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // the calling thread won the race and claimed the item.
            return task;
        }
        else
        {   // the calling thread lost the race and should try again.
            return INVALID_TASK_ID;
        }
    }
    else return INVALID_TASK_ID; // the queue is currently empty.
}

/// @summary Reset a task queue to empty.
/// @param queue The queue to clear.
internal_function inline void
TaskQueueClear
(
    TASK_QUEUE *queue
)
{
    queue->Public.store(0, std::memory_order_relaxed);
    queue->Private.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

/// @summary Allocate the memory for a task queue and initialize the queue to empty.
/// @param queue The task queue to initialize.
/// @param capacity The capacity of the queue. This value must be a power of two greater than zero.
/// @param arena The memory arena to allocate from. The caller should ensure that sufficient memory is available.
/// @return Zero if the queue is created successfully, or -1 if an error occurred.
internal_function int
NewTaskQueue
(
    TASK_QUEUE   *queue, 
    size_t     capacity, 
    MEMORY_ARENA *arena
)
{   // the capacity must be a power of two.
    assert((capacity & (capacity - 1)) == 0);
    queue->Public.store(0, std::memory_order_relaxed);
    queue->Private.store(0, std::memory_order_relaxed);
    queue->Mask  = int64_t(capacity) - 1;
    queue->Tasks = PushArray<task_id_t>(arena, capacity);
    return 0;
}

/// @summary Calculate the amount of memory required to store a task queue.
/// @param capacity The capacity of the queue.
/// @return The minimum number of bytes required to store the queue items, not including the size of the TASK_QUEUE instance.
internal_function size_t
CalculateMemoryForTaskQueue
(
    size_t capacity
)
{
    return sizeof(task_id_t) * capacity;
}

/// @summary Create a set of permits from a task dependency list, such that when all dependencies have completed, a task is ready-to-run.
/// @param thread_source The TASK_SOURCE of the thread creating the task @a task.
/// @param task The task identifier of the task being created.
/// @param work_item The work item data of the task being created.
/// @param deps The list of tasks that must complete before the new task becomes ready-to-run.
/// @param deps_count The number of task identifiers in the dependency list.
/// @return true if there is at least one outstanding permit on the new task, or false if the new task is ready-to-run.
internal_function bool
CreatePermits
(
    TASK_SOURCE     *thread_source, 
    task_id_t                 task, 
    TASK_DATA           *work_item, 
    task_id_t const           deps,
    size_t    const     deps_count
)
{
    TASK_SOURCE *slist = thread_source->TaskSources;
    work_item->Permits.store(-int32_t(deps_count), std::memory_order_relaxed);
    for (size_t i = 0; i < deps_count; ++i)
    {
        PERMITS_LIST *plist = GetTaskPermitsList(deps[i], slist);
        int32_t           n = plist->Count.load(std::memory_order_relaxed);
        do
        {
            if (n < 0)
            {   // this dependency has completed.
                // the fetch_add needs to be atomic because a permit added previously might complete.
                if (work_item->Permits.fetch_add(1, std::memory_order_acq_rel) == -1)
                {   // all dependencies have been satisfied for this task.
                    return 0;
                }
            }
            if (n < PERMITS_LIST::MAX_TASKS)
            {   // append the task ID to the permits list of deps[i].
                plist->Tasks[n] = task;
            }
            else
            {   // the best thing to do in this case is re-think your task breakdown.
                ConsoleError("ERROR (%S): Exceeded max permits on task %08X when defining task %08X (parent %08X).\n", __FUNCTION__, deps[i], task, work_item->ParentTask);
                assert(n < PERMITS_LIST::MAX_TASKS);
            }
        } while (plist->Count.compare_exchange_weak(n, n+1, std::memory_order_acq_rel, std::memory_order_relaxed));
    }
    return (deps_count > 0);
}

/// @summary Process the permits list for a completed task and move any now-permitted tasks to the ready-to-run queue(s).
/// @param thread_source The TASK_SOURCE of the calling thread.
/// @param task The identifier of the just-completed task.
/// @param permitted_compute On return, this location is incremented by the number of now-permitted tasks added to the compute task RTR queue for the task source.
/// @param permitted_general On return, this location is incremented by the number of now-permitted tasks added to the general task RTR queue for the task source.
/// @return The total number of now-permitted tasks.
internal_function size_t
AllowPermits
(
    TASK_SOURCE     *thread_source, 
    task_id_t                 task, 
    size_t      &permitted_compute, 
    size_t      &permitted_general
)
{
    size_t           nc = 0;
    size_t           ng = 0;
    TASK_SOURCE  *slist = thread_source->TaskSources;
    PERMITS_LIST *plist = GetTaskPermitsList(task, slist);
    TASK_QUEUE  *cqueue =&thread_source->ComputeWorkQueue;
    TASK_QUEUE  *gqueue =&thread_source->GeneralWorkQueue;
    int32_t           n = plist->Count.exchange(-1, std::memory_order_acq_rel);
    for (int32_t  i = 0;  i < n; ++i)
    {
        task_id_t  ref  = plist->Tasks[i];
        uint32_t   refs = (ref & TASK_ID_MASK_SOURCE_P) >> TASK_ID_SHIFT_SOURCE;
        uint32_t   refi = (ref & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
        bool is_compute = (ref & TASK_ID_MASK_POOL_P  ) == TASK_POOL_COMPUTE;
        TASK_DATA &refp = slist[refs].WorkItems[refi];
        if (refp.Permits.fetch_add(1, std::memory_order_acq_rel) == -1)
        {   // this task has no more outstanding permits and is ready-to-run.
            if (is_compute)
            {   // this task will run on the compute thread pool.
                TaskQueuePush(cqueue, ref); ++nc;
            }
            else
            {   // this task will run on the general thread pool.
                TaskQueuePush(gqueue, ref); ++ng;
            }
        }
    }
    permitted_compute += nc;
    permitted_general += ng;
    return (nc + ng);
}

/// @summary Implements the entry point of a thread pool worker thread that processes compute jobs.
/// @param argp A pointer to the WIN32_WORKER_THREAD instance specifying thread state.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
CompueWorkerMain
(
    void *argp
)
{
    WIN32_WORKER_THREAD *thread_data = (WIN32_WORKER_THREAD*) argp;
    WIN32_THREAD_POOL   *thread_pool =  thread_data->ThreadPool;
    MEMORY_ARENA       *thread_arena =  thread_data->ThreadArena;
    TASK_SOURCE       *thread_source =  thread_data->ThreadSource;
    std::atomic<int32_t>  *terminate =  thread_data->TerminateFlag;
    size_t                pool_index =  thread_data->PoolIndex;

    // indicate to the coordinator thread that this worker is ready-to-run.
    SetEvent(thread_data->ReadySignal);

    // wait for a signal to start executing tasks.
    if (WaitForSingleObject(thread_pool->LaunchSignal, INFINITE) != WAIT_OBJECT_0)
    {   // the wait was abandoned, or some other error occurred.
        ConsoleError("ERROR (%S): Wait for launch signal failed on compute worker %zu (%08X).\n", __FUNCTION__, pool_index, GetLastError());
        goto terminate_worker;
    }
    // check for early termination by the coordinator thread.
    if (terminate->load(std::memory_order_seq_cst) != 0)
    {   // early termination was signaled, likely because of an error elsewhere.
        ConsoleOutput("STATUS (%S): Early termination signal received on compute worker %zu.\n", __FUNCTION__, pool_index);
        goto terminate_worker;
    }

    // enter the main work loop for the worker thread.
    // note that this thread is the *only* thread that can put work in its own queue(s).
    // other threads may wake this worker if they have work items that can be stolen.
    for ( ; ; )
    {   // wait until someone has work available for this thread to attempt to steal.
        TASK_SOURCE *victim = WorkerThreadWaitForWakeup(thread_pool, pool_index);
        if (victim == NULL)
        {   // this is just a general notification. check for a termination signal.
            if (terminate->load(std::memory_order_seq_cst) != 0)
            {   // termination was signaled; shut down normally.
                goto terminate_worker;
            }
        }
    }

terminate_worker:
    return 0;
}

/// @summary Implements the entry point of a thread pool worker thread that processes general asynchronous jobs.
/// @param argp A pointer to the WIN32_WORKER_THREAD instance specifying thread state.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
GeneralWorkerMain
(
    void *argp
)
{
    WIN32_WORKER_THREAD *thread_data = (WIN32_WORKER_THREAD*) argp;
    WIN32_THREAD_POOL   *thread_pool =  thread_data->ThreadPool;
    MEMORY_ARENA       *thread_arena =  thread_data->ThreadArena;
    TASK_SOURCE       *thread_source =  thread_data->ThreadSource;
    std::atomic<int32_t>  *terminate =  thread_data->TerminateFlag;
    size_t                pool_index =  thread_data->PoolIndex;

    // indicate to the coordinator thread that this worker is ready-to-run.
    SetEvent(thread_data->ReadySignal);

    // wait for a signal to start executing tasks.
    if (WaitForSingleObject(thread_pool->LaunchSignal, INFINITE) != WAIT_OBJECT_0)
    {   // the wait was abandoned, or some other error occurred.
        ConsoleError("ERROR (%S): Wait for launch signal failed on general worker %zu (%08X).\n", __FUNCTION__, pool_index, GetLastError());
        goto terminate_worker;
    }
    // check for early termination by the coordinator thread.
    if (terminate->load(std::memory_order_seq_cst) != 0)
    {   // early termination was signaled, likely because of an error elsewhere.
        ConsoleOutput("STATUS (%S): Early termination signal received on general worker %zu.\n", __FUNCTION__, pool_index);
        goto terminate_worker;
    }

    // enter the main work loop for the worker thread.
    // note that this thread is the *only* thread that can put work in its own queue(s).
    // other threads may wake this worker if they have work items that can be stolen.
    for ( ; ; )
    {   // wait until someone has work available for this thread to attempt to steal.
        TASK_SOURCE *victim = WorkerThreadWaitForWakeup(thread_pool, pool_index);
        if (victim == NULL)
        {   // this is just a general notification. check for a termination signal.
            if (terminate->load(std::memory_order_seq_cst) != 0)
            {   // termination was signaled; shut down normally.
                goto terminate_worker;
            }
        }
    }

terminate_worker:
    return 0;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Calculate the amount of memory required for a thread pool, not including the per-thread memory arena.
/// @param max_threads The maximum number of threads in the pool.
/// @return The number of bytes required for thread pool initialization, not including the size of the WIN32_THREAD_POOL instance.
public_function size_t
CalculateMemoryForThreadPool
(
    size_t max_threads
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += AllocationSizeForArray<unsigned int>(max_threads);
    size_in_bytes += AllocationSizeForArray<HANDLE>(max_threads);
    size_in_bytes += AllocationSizeForArray<WIN32_MEMORY_ARENA>(max_threads);
    size_in_bytes += AllocationSizeForArray<WIN32_WORKER_THREAD>(max_threads);
    size_in_bytes += AllocationSizeForArray<TASK_SOURCE*>(max_threads);
    size_in_bytes += AllocationSizeForArray<MEMORY_ARENA>(max_threads);
    return size_in_bytes;
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
    if (!ArenaCanAllocate(arena, mem_required, std::alignment_of<WIN32_THREAD_POOL>::value))
    {
        ConsoleError("ERROR (%S): Insufficient memory to initialize thread pool; need %zu bytes.\n", __FUNCTION__, mem_required);
        return -1;
    }

    // initialize the thread pool state and allocate memory for variable-length arrays.
    thread_pool->MaxThreads               = pool_config->MaxThreads;
    thread_pool->ActiveThreads            = 0;
    thread_pool->WorkerArenaSize          = pool_config->WorkerArenaSize;
    thread_pool->CompletionPort           = NULL;
    thread_pool->LaunchSignal             = pool_config->LaunchSignal;
    thread_pool->MainThreadArgs           = pool_config->MainThreadArgs;
    thread_pool->OSThreadIds              = PushArray<unsigned int       >(arena, pool_config->MaxThreads);
    thread_pool->OSThreadHandle           = PushArray<HANDLE             >(arena, pool_config->MaxThreads);
    thread_pool->OSThreadArena            = PushArray<WIN32_MEMORY_ARENA >(arena, pool_config->MaxThreads);
    thread_pool->WorkerState              = PushArray<WIN32_WORKER_THREAD>(arena, pool_config->MaxThreads);
    thread_pool->WorkerSource             = PushArray<WIN32_TASK_SOURCE *>(arena, pool_config->MaxThreads);
    thread_pool->WorkerArena              = PushArray<MEMORY_ARENA       >(arena, pool_config->MaxThreads);
    thread_pool->WorkerMain               = pool_config->ThreadMain;
    thread_pool->TaskScheduler            = pool_config->TaskScheduler;
    thread_pool->WorkerFlags              = pool_config->WorkerFlags;
    ZeroMemory(thread_pool->OSThreadIds   , pool_config->MaxThreads * sizeof(unsigned int));
    ZeroMemory(thread_pool->OSThreadHandle, pool_config->MaxThreads * sizeof(HANDLE));
    ZeroMemory(thread_pool->OSThreadArena , pool_config->MaxThreads * sizeof(WIN32_MEMORY_ARENA));
    ZeroMemory(thread_pool->WorkerState   , pool_config->MaxThreads * sizeof(WIN32_WORKER_THREAD));
    ZeroMemory(thread_pool->WorkerSource  , pool_config->MaxThreads * sizeof(WIN32_TASK_SOURCE *));
    ZeroMemory(thread_pool->WorkerArena   , pool_config->MaxThreads * sizeof(MEMORY_ARENA));
    thread_pool->TerminateFlag.store(0, std::memory_order_relaxed);

    // create a new completion port used to wake the threads in the pool.
    if ((thread_pool->CompletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, (DWORD) pool_config->MaxThreads)) == NULL)
    {
        ConsoleError("ERROR (%S): Unable to create I/O completion port for thread pool (%08X).\n", __FUNCTION__, GetLastError());
        goto cleanup_and_fail;
    }

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

    // initialize the array of pointers to TASK_SOURCE objects.
    // the pointers point back into the scheduler's SourceList.
    for (size_t i = 0; i < pool_config->MaxThreads; ++i)
    {
        size_t src_index = pool_config->WorkerSourceIndex + i;
        thread_pool->WorkerSource[i] = &pool_config->TaskScheduler->SourceList[src_index];
    }

    // spawn workers until the maximum thread count is reached.
    // it might be nice to re-introduce the concept of transient threads, which 
    // die after some period of idle-ness, but that may not be work the implementation complexity.
    // idle workers will just sit in a wait state until there's work for them to do.
    for (size_t i = 0; i < pool_config->MaxThreads; ++i)
    {
        if (SpawnWorkerThread(thread_pool, i, pool_config->WorkerFlags) < 0)
        {   // unable to spawn the worker thread; the minimum pool size cannot be met.
            goto cleanup_and_fail;
        }
    }
    return 0;

cleanup_and_fail:
    if (thread_pool->ActiveThreads > 0)
    {   // signal all threads in the pool to die.
        DWORD   n = (DWORD) thread_pool->ActiveThreads;
        HANDLE *h =  thread_pool->OSThreadHandle;
        thread_pool->TerminateFlag.store(0, std::memory_order_seq_cst);
        WakeAllWorkerThreads(thread_pool);
        WaitForMultipleObjects(n, h, TRUE, INFINITE);
    }
    if (thread_pool->CompletionPort != NULL)
    {
        CloseHandle(thread_pool->CompletionPort);
        thread_pool->CompletionPort = NULL;
    }
    if (pool_config->WorkerArenaSize > 0 && thread_pool->OSThreadArena != NULL)
    {   // free the reserved and committed address space for thread-local arenas.
        for (size_t i = 0; i < pool_config->MaxThreads; ++i)
        {   // no cleanup needs to be performed for the 'user-facing' arena.
            DeleteMemoryArena(&thread_pool->OSThreadArena[i]);
        }
    }
    ArenaResetToMarker(arena, mem_marker);
    return -1;
}

/// @summary Signal all threads in a thread pool to terminate, and block the calling thread until all workers have exited.
/// @param thread_pool The thread pool to terminate.
public_function void
TerminateThreadPool
(
    WIN32_THREAD_POOL *thread_pool
)
{
    DWORD n = (DWORD) thread_pool->ActiveThreads;
    
    // signal all threads in the pool to terminate.
    if (thread_pool->ActiveThreads > 0)
    {   // signal the threads, which could be in the middle of executing jobs.
        HANDLE *h =  thread_pool->OSThreadHandle;
        thread_pool->TerminateFlag.store(1, std::memory_order_seq_cst);
        WakeAllWorkerThreads(thread_pool);
        // block the calling thread until all threads in the pool have terminated.
        WaitForMultipleObjects(n, h, TRUE, INFINITE);
        thread_pool->ActiveThreads = 0;
    }

    // free the reserved and committed address space for thread-local arenas.
    if (thread_pool->OSThreadArena != NULL)
    {
        for (size_t i = 0; i < thread_pool->MaxThreads; ++i)
        {   // no cleanup needs to be performed for the 'user-facing' arena.
            DeleteMemoryArena(&thread_pool->OSThreadArena[i]);
        }
        ZeroMemory(thread_pool->OSThreadArena, pool_config->MaxThreads * sizeof(WIN32_MEMORY_ARENA));
        ZeroMemory(thread_pool->WorkerArena  , pool_config->MaxThreads * sizeof(MEMORY_ARENA));
    }
    // NOTE: intentionally not closing the completion port here.
    // it's possible that another pool is still live, and that pool might post to this pool.
}

/// @summary Close the completion port associated with a thread pool. ALl worker threads must be stopped (with TerminateThreadPool) prior to calling this function.
/// @param thread_pool The thread pool to delete.
public_function void
DeleteThreadPool
(
    WIN32_THREAD_POOL *thread_pool
)
{   assert(thread_pool->ActiveThreads == 0);
    if (thread_pool->CompletionPort != NULL)
    {   // also close the completion port at this point.
        CloseHandle(thread_pool->CompletionPort);
        thread_pool->CompletionPort = NULL;
    }
}

/// @summary Calculate the amount of memory required to store task source data.
/// @param max_tasks The maximum number of tasks that can be live at any one time.
/// @return The minimum number of bytes required to store the task data, not including the size of the TASK_SOURCE instance.
public_function size_t
CalculateMemoryForTaskSource
(
    size_t max_tasks
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += CalculateMemoryForTaskQueue(max_tasks);
    size_in_bytes += CalculateMemoryForTaskQueue(max_tasks);
    size_in_bytes += AllocationSizeForArray<TASK_DATA>(max_tasks);
    size_in_bytes += AllocationSizeForArray<int32_t>(max_tasks);
    size_in_bytes += AllocationSizeForArray<PERMITS_LIST>(max_tasks);
    return size_in_bytes;
}

/// @summary Allocate and initialize a TASK_SOURCE from a scheduler instance.
/// @param scheduler The scheduler instance that will monitor the work source.
/// @param max_tasks The maximum number of tasks that can be alive at any given time.
/// @param arena The memory arena from which 'global' memory will be allocated.
/// @return A pointer to the initialized WIN32_TASK_SOURCE, or NULL.
public_function TASK_SOURCE*
NewTaskSource
(
    WIN32_TASK_SCHEDULER  *scheduler, 
    size_t                 max_tasks, 
    MEMORY_ARENA              *arena
)
{
    if (max_tasks < 1)
    {   // inherit the default value from the scheduler.
        max_tasks = scheduler->MaxTasks;
    }
    if ((max_tasks & (max_tasks - 1)) != 0)
    {   // this value must be a power-of-two. round up to the next multiple.
        size_t n = 1;
        size_t m = max_tasks;
        while (n < m)
        {
            n <<= 1;
        }
        max_tasks = n;
    }
    if (max_tasks > MAX_TASKS_PER_SOURCE)
    {   // consider this to be an error; it's easily trapped during development.
        ConsoleError("ERROR (%S): Requested task buffer size %zu exceeds maximum %u.\n", __FUNCTION__, max_tasks, MAX_TASKS_PER_SOURCE);
        return NULL;
    }
    if (scheduler->SourceCount == scheduler->MaxSources)
    {   // no additional sources can be allocated from the scheduler.
        ConsoleError("ERROR (%S): Max sources exceeded; increase limit WIN32_TASK_SCHEDULER_CONFIG::MaxTaskSources.\n", __FUNCTION__);
        return NULL;
    }

    size_t   mem_marker = ArenaMarker(arena);
    size_t bytes_needed = CalculateMemoryForTaskSource(max_tasks);
    size_t    alignment = std::alignment_of<TASK_SOURCE>::value;
    if (!ArenaCanAllocate(arena, bytes_needed, alignment))
    {   // the arena doesn't have sufficient memory to initialize a source with the requested attributes.
        ConsoleError("ERROR (%S): Insufficient memory to allocate source; need %zu bytes.\n", __FUNCTION__, bytes_needed);
        return NULL;
    }

    TASK_SOURCE      *source = &scheduler->SourceList[scheduler->SourceCount];
    if (NewTaskQueue(&source->ComputeWorkQueue, max_tasks, arena) < 0)
    {   // unable to initialize the compute task queue for the source.
        ConsoleError("ERROR (%S): Failed to create compute task queue for source.\n", __FUNCTION__);
        ArenaResetToMarker(arena, mem_marker);
        return NULL;
    }
    if (NewTaskQueue(&source->GeneralWorkQueue, max_tasks, arena) < 0)
    {   // unable to initialize the general task queue for the source.
        ConsoleError("ERROR (%S): Failed to create general task queue for source.\n", __FUNCTION__);
        ArenaResetToMarker(arena, mem_marker);
        return NULL;
    }
    source->ComputePoolPort = scheduler->ComputePool.CompletionPort;
    source->GeneralPoolPort = scheduler->GeneralPool.CompletionPort;
    source->SourceIndex     = uint32_t(scheduler->SourceCount);
    source->SourceCount     = uint32_t(scheduler->MaxSources);
    source->TaskSources     = scheduler->SourceList;
    source->MaxTasks        = uint32_t(max_tasks);
    source->TaskIndex       = 0;
    source->WorkItems       = PushArray<TASK_DATA   >(arena, max_tasks);
    source->WorkCount       = PushArray<int32_t     >(arena, max_tasks);
    source->PermitList      = PushArray<PERMITS_LIST>(arena, max_tasks);
    scheduler->SourceCount++;
    return source;
}

