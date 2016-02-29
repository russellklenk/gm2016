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
    task_id_t                TaskId;             /// The task identifier.
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

/// @summary Define the state data associated with a single worker in a thread pool.
struct WIN32_WORKER_THREAD
{
    WIN32_THREAD_POOL       *ThreadPool;         /// The thread pool that owns the worker thread.
    MEMORY_ARENA            *ThreadArena;        /// The thread-local memory arena.
    TASK_SOURCE             *ThreadSource;       /// The TASK_SOURCE allocated to the worker thread.
    HANDLE                   ReadySignal;        /// A manual-reset event, created by the pool and signaled by the worker when the worker becomes ready-to-run.
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
    HANDLE                   TerminateSignal;    /// Signal set by the coordinator to stop all active worker threads.
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
CalculateMemoryForComputeTaskQueue
(
    size_t capacity
)
{
    return sizeof(task_id_t) * capacity;
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

