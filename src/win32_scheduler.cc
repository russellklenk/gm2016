/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement two task schedulers. One implementation is designed for
/// non-blocking, compute-oriented tasks. The other implementation is designed 
/// for more traditional asynchronous tasks such as database calls.
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Define the mask and shift values for the constituient parts of a compute task ID.
/// The task ID layout is as follows:
/// 31.............................0
/// VWWWWWWWWWWWWIIIIIIIIIIIIIIIIPBB
/// 
/// Where the letters mean the following:
/// V: Set if the ID is valid, clear if the ID is not valid.
/// W: The zero-based index of the worker thread. The system supports up to 4095 worker threads, plus 1 to represent the thread that submits the root tasks.
/// I: The zero-based index of the task data within the worker thread's task buffer.
/// P: Set if the task executes on the blocking pool, clear if the task executes on the non-blocking pool.
/// B: The zero-based index of the task buffer. The system supports up to 4 ticks in-flight simultaneously.
#ifndef TASK_ID_LAYOUT_DEFINED
#define TASK_ID_LAYOUT_DEFINED
#define TASK_ID_MASK_BUFFER_P             (0x00000003UL)
#define TASK_ID_MASK_BUFFER_U             (0x00000003UL)
#define TASK_ID_MASK_POOL_P               (0x00000004UL)
#define TASK_ID_MASK_POOL_U               (0x00000001UL)
#define TASK_ID_MASK_INDEX_P              (0x0007FFF8UL)
#define TASK_ID_MASK_INDEX_U              (0x0000FFFFUL)
#define TASK_ID_MASK_THREAD_P             (0x7FF80000UL)
#define TASK_ID_MASK_THREAD_U             (0x00000FFFUL)
#define TASK_ID_MASK_VALID_P              (0x80000000UL)
#define TASK_ID_MASK_VALID_U              (0x00000001UL)
#define TASK_ID_SHIFT_BUFFER              (0)
#define TASK_ID_SHIFT_POOL                (2)
#define TASK_ID_SHIFT_INDEX               (3)
#define TASK_ID_SHIFT_THREAD              (19)
#define TASK_ID_SHIFT_VALID               (31)
#endif

/// @summary Define the maximum number of worker threads supported by the scheduler.
#ifndef MAX_WORKER_THREADS
#define MAX_WORKER_THREADS                (4095)
#endif

/// @summary Define the maximum number of threads that can submit jobs to the scheduler. This is the number of worker threads plus one, to account for the main thread that submits the root tasks.
#ifndef MAX_SCHEDULER_THREADS
#define MAX_SCHEDULER_THREADS             (4096)
#endif

/// @summary Define the maximum number of task storage buffers. The runtime limit may be lower.
#ifndef MAX_TASK_BUFFERS
#define MAX_TASK_BUFFERS                  (4)
#endif

/// @summary Define the maximum number of tasks that can be stored in a single task buffer. The runtime limit may be lower.
#ifndef MAX_TASKS_PER_BUFFER
#define MAX_TASKS_PER_BUFFER              (65536)
#endif

/// @summary Define a special value used to indicate that no thread-local memory is required.
#ifndef WORKER_THREAD_ARENA_NOT_NEEDED
#define WORKER_THREAD_ARENA_NOT_NEEDED    (~size_t(0))
#endif

/// @summary Define the default size of the thread-local memory for each worker thread.
#ifndef WORKER_THREAD_ARENA_SIZE_DEFAULT
#define WORKER_THREAD_ARENA_SIZE_DEFAULT  (2UL * 1024UL * 1024UL)
#endif

/// @summary Define the identifier returned to represent an invalid task ID.
#ifndef INVALID_TASK_ID
#define INVALID_TASK_ID                   ((task_id_t) 0x7FFFFFFFUL)
#endif

/// @summary Macro to select the lesser of two values.
#ifndef SCHEDULER_MIN
#define SCHEDULER_MIN(a, b)               (((a) <= (b)) ? (a) : (b))
#endif

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/
struct WIN32_THREAD_ARGS;
struct WIN32_MEMORY_ARENA;
struct WIN32_TASK_SCHEDULER;
struct WIN32_TASK_SCHEDULER_CONFIG;
struct WIN32_WORKER_SIGNAL;
struct WIN32_WORKER_THREAD;
struct WIN32_THREAD_POOL;
struct WIN32_THREAD_POOL_CONFIG;
struct WIN32_TASK_SOURCE;

struct MEMORY_ARENA;

struct GENERAL_TASK_QUEUE;
struct COMPUTE_TASK_QUEUE;
struct GENERAL_TASK_DATA;
struct COMPUTE_TASK_DATA;
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

/// @summary Define the function signature for a task entry point that runs on the general thread pool.
/// @param task_id The identifier of the task being executed.
/// @param thread_source A COMPUTE_TASK_SOURCE associated with the thread that can be used to launch compute jobs.
/// @param work_item A thread-local copy of the data associated with the task to execute.
/// @param thread_arena A thread-local memory arena that can be used to allocate working memory. The arena is reset after the entry point returns.
/// @param main_args Global data managed by the main application thread.
/// @param scheduler The scheduler that owns the task being executed.
typedef void (*GENERAL_TASK_ENTRYPOINT)
(
    task_id_t                        task_id, 
    WIN32_COMPUTE_TASK_SOURCE *thread_source, 
    GENERAL_TASK_DATA             *work_item, 
    MEMORY_ARENA               *thread_arena, 
    WIN32_THREAD_ARGS             *main_args, 
    WIN32_TASK_SCHEDULER          *scheduler
);

/// @summary Define the function signature for a task entry point that runs on the compute thread pool.
/// @param task_id The identifier of the task being executed.
/// @param thread_source A COMPUTE_TASK_SOURCE associated with the thread that can be used to launch compute jobs.
/// @param work_item A thread-local copy of the data associated with the task to execute.
/// @param thread_arena A thread-local memory arena that can be used to allocate working memory. The arena is reset after the entry point returns.
/// @param main_args Global data managed by the main application thread.
/// @param scheduler The scheduler that owns the task being executed.
typedef void (*COMPUTE_TASK_ENTRYPOINT)
(
    task_id_t                        task_id, 
    WIN32_COMPUTE_TASK_SOURCE *thread_source, 
    COMPUTE_TASK_DATA             *work_item, 
    MEMORY_ARENA               *thread_arena, 
    WIN32_THREAD_ARGS             *main_args, 
    WIN32_TASK_SCHEDULER          *scheduler
);

/// @summary Define bitflags controlling worker thread behavior.
enum WORKER_FLAGS : uint32_t
{
    WORKER_FLAGS_NONE       = (0UL << 0),        /// The worker thread has the default behavior.
    WORKER_FLAGS_TRANSIENT  = (1UL << 0),        /// The worker thread is transient; that is, it self-terminates after a fixed interval with no work.
    WORKER_FLAGS_GENERAL    = (1UL << 1),        /// The worker thread can execute general tasks.
    WORKER_FLAGS_COMPUTE    = (1UL << 2),        /// The worker thread can execute compute tasks.
};

/// @summary Define identifiers for task ID validity. An ID can only be valid or invalid.
enum TASK_ID_TYPE : uint32_t
{
    TASK_ID_TYPE_INVALID    = 0,                 /// The task identifier specifies an invalid task.
    TASK_ID_TYPE_VALID      = 1,                 /// The task identifier specifies a valid task.
};

/// @summary Define identifiers for supported task pools. A task is either blocking or non-blocking.
enum TASK_POOL : uint32_t
{
    TASK_POOL_COMPUTE       = 0,                 /// The task executes on the compute thread pool, designed for non-blocking, work-heavy tasks.
    TASK_POOL_GENERAL       = 1,                 /// The task executes on the general thread pool, designed for blocking, light-CPU tasks.
    TASK_POOL_COUNT         = 2                  /// The number of thread pools defined by the scheduler.
};

/// @summary Define the data associated with a spinning semaphore, which is guaranteed to remain in user mode unless a thread must be put to sleep or woken.
struct SEMAPHORE
{   static size_t const     PAD                    = CACHELINE_SIZE - sizeof(std::atomic<int32_t>);
    std::atomic<int32_t>    Count;               /// The current count of the semaphore.
    uint8_t                 Padding[PAD];        /// Unused padding out to the next cacheline boundary.
    int32_t                 SpinCount;           /// The spin count assigned to the semaphore at creation time.
    HANDLE                  KSem;                /// The handle of the kernel semaphore object.
};

/// @summary Define a structure specifying the constituent parts of a task ID.
struct TASK_ID_PARTS
{
    uint32_t                ValidTask;           /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t                PoolType;            /// One of TASK_POOL specifying the thread pool that executes the task.
    uint32_t                ThreadIndex;         /// The zero-based index of the thread that defines the task.
    uint32_t                BufferIndex;         /// The zero-based index of the thread-local buffer that defines the task.
    uint32_t                TaskIndex;           /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define the data associated with a task that runs on the general thread pool.
/// These tasks are expected to have a light CPU work load, and may perform blocking, long-running operations.
struct GENERAL_TASK_DATA
{   static size_t const     ALIGNMENT              = CACHELINE_SIZE;
    static size_t const     MAX_DATA               = 48;
    std::atomic<uint32_t>   Sequence;            /// A sequence value assigned by the GENERAL_TASK_QUEUE.
    task_id_t               TaskId;              /// The task identifier.
    GENERAL_TASK_ENTRYPOINT TaskMain;            /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t                Reserved1;           /// Padding; unused.
#endif
    uint8_t                 Data[MAX_DATA];      /// User-supplied argument data associated with the work item.
};

/// @summary Define the data associated with a task that runs on the compute thread pool.
/// These tasks are expected to be CPU-heavy and not perform any blocking or long-running operations.
struct COMPUTE_TASK_DATA
{   static size_t const     ALIGNMENT              = CACHELINE_SIZE;
    static size_t const     MAX_DATA               = 48;
    task_id_t               TaskId;              /// The task identifier.
    task_id_t               ParentTask;          /// The identifier of the parent task, or INVALID_TASK_ID.
    COMPUTE_TASK_ENTRYPOINT TaskMain;            /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t                Reserved1;           /// Padding; unused.
#endif
    uint8_t                 Data[MAX_DATA];      /// User-supplied argument data associated with the work item.
};

/// @summary Define the data representing a work queue safe for access by multiple concurrent producers and multiple concurrent consumers.
/// The work item data is stored directly within the queue. All tasks in the queue are in the ready-to-run state. A single queue feeds all workers.
struct GENERAL_TASK_QUEUE
{   static size_t const     ALIGNMENT              = CACHELINE_SIZE;
    static size_t const     PAD                    = CACHELINE_SIZE - sizeof(std::atomic<uint32_t>);
    std::atomic<uint32_t>   Tail;                /// The index at which items are enqueued, representing the tail of the queue.
    uint8_t                 Pad0[PAD];           /// Padding separating producer data from consumer data.
    std::atomic<uint32_t>   Head;                /// The index at which items are dequeued, representing the head of the queue.
    uint8_t                 Pad1[PAD];           /// Padding separating consumer data from shared data.
    uint32_t                Mask;                /// The bitmask used to map the Head and Tail indices into the storage array.
    GENERAL_TASK_DATA      *WorkItems;           /// The data associated with each ready-to-run work item.
};

/// @summary Define the data representing a work-stealing deque of task identifiers. Each compute worker thread maintains its own queue.
/// The worker thread can perform push and take operations. Other worker threads can perform concurrent steal operations.
struct COMPUTE_TASK_QUEUE
{   static size_t const     ALIGNMENT              = CACHELINE_SIZE;
    static size_t const     PAD                    = CACHELINE_SIZE - sizeof(std::atomic<int64_t>);
    std::atomic<int64_t>    Public;              /// The public end of the deque, updated by steal operations (Top).
    uint8_t                 Pad0[PAD];           /// Padding separating the public data from the private data.
    std::atomic<int64_t>    Private;             /// The private end of the deque, updated by push and take operations (Bottom).
    uint8_t                 Pad1[PAD];           /// Padding separating the private data from the storage data.
    int64_t                 Mask;                /// The bitmask used to map the Top and Bottom indices into the storage array.
    task_id_t              *Tasks;               /// The identifiers for the tasks in the queue.
};

/// @summary Defines the data associated with a set of tasks waiting on another task to complete.
/// Only tasks in the compute queue support dependencies and permits.
struct PERMITS_LIST
{   static size_t const     ALIGNMENT              = CACHELINE_SIZE;
    static size_t const     MAX_TASKS              = 15;
    std::atomic<int32_t>    Count;               /// The number of items in the permits list.
    task_id_t               Tasks[MAX_TASKS];    /// The task IDs in the permits list. This is the set of tasks to launch when the owning task completes.
};

/// @summary Define the signals that can be sent to or set by a worker thread.
struct WIN32_WORKER_SIGNAL
{
    HANDLE                   ReadySignal;        /// Signal set by the worker to indicate that its initialization has completed and it is ready to run.
    HANDLE                   StartSignal;        /// Signal set by the coordinator to allow the worker to start running.
    HANDLE                   ErrorSignal;        /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                   TerminateSignal;    /// Signal set by the coordinator to stop all worker threads.
};

/// @summary Define the state data associated with a single worker in a thread pool.
struct WIN32_WORKER_THREAD
{
    WIN32_TASK_SCHEDULER    *TaskScheduler;      /// The task scheduler that owns the thread pool.
    WIN32_TASK_SOURCE       *ThreadSource;       /// The WIN32_TASK_SOURCE assigned to the worker thread.
    MEMORY_ARENA            *ThreadArena;        /// The thread-local memory arena.
    WIN32_THREAD_POOL       *ThreadPool;         /// The thread pool that owns the worker thread.
    uint32_t                 PoolIndex;          /// The zero-based index of the thread in the owning thread pool.
    uint32_t                 WorkerFlags;        /// WORKER_FLAGS controlling worker thread behavior.
    WIN32_WORKER_SIGNAL      Signals;            /// The signals that can be sent to or set by a worker thread.
    WIN32_THREAD_ARGS       *MainThreadArgs;     /// The global data managed by the main thread and available to all threads.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint8_t                  Padding[20];        /// Padding out to the end of the 64-byte cacheline.
#endif
    uint8_t                  StartData[64];      /// Additional data to be passed to the thread at startup, for example, a task to execute.
};

/// @summary Define the state data associated with a pool of threads.
struct WIN32_THREAD_POOL
{
    size_t                   MaxThreads;         /// The maximum number of threads in the pool.
    size_t                   ActiveThreads;      /// The number of active threads in the pool. 
    size_t                   WorkerArenaSize;    /// The size of each thread-local memory arena, in bytes.
    HANDLE                   StartSignal;        /// Manual-reset event signaled by the coordinator to allow the worker to start running.
    HANDLE                   ErrorSignal;        /// Manual-reset event signaled by any thread to indicate that a fatal error has occurred.
    HANDLE                   TerminateSignal;    /// Manual-reset event signaled by the coordinator to terminate all worker threads.
    WIN32_THREAD_ARGS       *MainThreadArgs;     /// The global data managed by the main thread and available to all threads.
    unsigned int            *OSThreadIds;        /// The operating system identifiers for each worker thread.
    HANDLE                  *OSThreadHandle;     /// The operating system thread handle for each worker thread.
    WIN32_MEMORY_ARENA      *OSThreadArena;      /// The underlying OS memory arena for each worker thread.
    WIN32_WORKER_THREAD     *WorkerState;        /// The state data for each worker thread.
    WIN32_TASK_SOURCE      **WorkerSource;       /// The TASK_SOURCE assigned to each worker thread.
    MEMORY_ARENA            *WorkerArena;        /// The thread-local memory arena assigned to each worker thread.
    uint32_t                *WorkerFreeList;     /// The list of indices of available worker threads.
    uint32_t                 WorkerFreeCount;    /// The number of pool indices in the transient worker free list.
    WIN32_WORKER_ENTRYPOINT  WorkerMain;         /// The entry point for all threads in the pool.
    WIN32_TASK_SCHEDULER    *TaskScheduler;      /// The scheduler that owns the thread pool, or NULL.
    uint32_t                 FlagsTransient;     /// The WORKER_FLAGS to apply to transient worker threads.
    uint32_t                 FlagsPersistent;    /// The WORKER_FLAGS to apply to persistent worker threads.
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
    HANDLE                   StartSignal;        /// Signal set by the coordinator to allow all active worker threads to start running.
    HANDLE                   ErrorSignal;        /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                   TerminateSignal;    /// Signal set by the coordinator to stop all active worker threads.
    uint32_t                 FlagsTransient;     /// The WORKER_FLAGS to apply to transient threads (WORKER_FLAGS_TRANSIENT is implied.)
    uint32_t                 FlagsPersistent;    /// The WORKER_FLAGS to apply to persistent threads.
};

/// @summary Define the data associated with a thread that can produce compute tasks (but not necessarily execute them.)
/// Each worker thread in the scheduler thread pool is a COMPUTE_TASK_SOURCE that can also execute tasks.
/// The maximum number of task sources is fixed at scheduler creation time.
struct cacheline_align WIN32_TASK_SOURCE
{   static size_t const      ALIGNMENT              = CACHELINE_SIZE;

    COMPUTE_TASK_QUEUE       WorkQueue;          /// The queue of ready-to-run task IDs.

    SEMAPHORE                StealSignal;        /// An auto-reset event used to wake a single thread when work can be stolen.
    size_t                   SourceIndex;        /// The zero-based index of the source within the source list.
    size_t                   SourceCount;        /// The total number of task sources defined in the scheduler. Constant.
    WIN32_TASK_SOURCE       *TaskSources;        /// The list of per-source state for each task source. Managed by the scheduler.
    
    uint32_t                 TasksPerBuffer;     /// The allocation capacity of a single task buffer.
    uint32_t                 BufferIndex;        /// The zero-based index of the task buffer being written to.
    uint32_t                 TaskCount;          /// The zero-based index of the next available task in the current buffer.

    COMPUTE_TASK_DATA       *WorkItems [2];      /// The work item definitions for each task, for each buffer.
    int32_t                 *WorkCounts[2];      /// The outstanding work counter for each task, for each buffer.
    PERMITS_LIST            *PermitList[2];      /// The permits list for each task, for each buffer.
};

/// @summary Define the data associated with a compute-oriented task scheduler.
struct WIN32_TASK_SCHEDULER
{
    size_t                   MaxSources;         /// The maximum number of compute task sources.
    size_t                   SourceCount;        /// The number of allocated compute task sources.
    WIN32_TASK_SOURCE       *SourceList;         /// The data associated with each compute task source. SourceCount are currently valid.

    WIN32_THREAD_POOL        GeneralPool;        /// The thread pool used for running light-work asynchronous tasks.
    WIN32_THREAD_POOL        ComputePool;        /// The thread pool used for running work-heavy, non-blocking tasks.

    HANDLE                   StartSignal;        /// Manual-reset event signaled when worker threads should start running tasks.
    HANDLE                   ErrorSignal;        /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE                   TerminateSignal;    /// Manual-reset event signaled when the scheduler is being shut down.
};

/// @summary Define the user-facing thread pool configuration data.
struct WIN32_THREAD_POOL_SIZE
{
    size_t                   MinThreads;         /// The minimum number of threads in the thread pool.
    size_t                   MaxThreads;         /// The maximum number of threads in the thread pool.
    size_t                   MaxTasks;           /// The maximum number of tasks that any worker thread in the pool can have active at any given time.
    size_t                   ArenaSize;          /// The size of the thread-local memory arena, in bytes.
};

/// @summary Define a structure used to specify data used to configure a task scheduler instance at creation time.
struct WIN32_TASK_SCHEDULER_CONFIG
{   static size_t const      NUM_POOLS             = TASK_POOL_COUNT;
    size_t                   MaxTaskSources;     /// The maximum number of threads (task sources) that can create tasks.
    WIN32_THREAD_POOL_SIZE   PoolSize[NUM_POOLS];/// The maximum number of worker threads in each type of thread pool.
};

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Create a task ID from its constituient parts.
/// @param buffer_index The zero-based index of the task buffer.
/// @param pool_type The type of thread pool that owns the task. One of TASK_POOL.
/// @param task_index The zero-based index of the task within the task list for this tick in the thread that created the task.
/// @param thread_index The zero-based index of the thread that created the task.
/// @param task_id_type Indicates whether the task ID is valid. One of TASK_ID_TYPE.
/// @return The task identifier.
internal_function inline task_id_t
MakeTaskId
(
    uint32_t   buffer_index, 
    uint32_t      pool_type, 
    uint32_t     task_index, 
    uint32_t   thread_index, 
    uint32_t   task_id_type = TASK_ID_TYPE_VALID
)
{
    return ((buffer_index   & TASK_ID_MASK_BUFFER_U) << TASK_ID_SHIFT_BUFFER) | 
           ((scheduler_type & TASK_ID_MASK_POOL_U  ) << TASK_ID_SHIFT_POOL  ) | 
           ((task_index     & TASK_ID_MASK_INDEX_U ) << TASK_ID_SHIFT_INDEX ) |
           ((thread_index   & TASK_ID_MASK_THREAD_U) << TASK_ID_SHIFT_THREAD) | 
           ((task_id_type   & TASK_ID_MASK_VALID_U ) << TASK_ID_SHIFT_VALID );
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
/// @param id The task identifier.
/// @return true if the task identifier specifies a valid task that will execute in the general thread pool.
internal_function inline bool 
IsGeneralTask
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0) && 
           (((id & TASK_ID_MASK_POOL_P ) >> TASK_ID_SHIFT_POOL ) == TASK_POOL_GENERAL);
}

/// @summary Determine whether an ID identifies a valid task that will execute in the compute thread pool.
/// @param id The task identifier.
/// @return true if the task identifier specifies a valid task that will execute in the compute thread pool.
internal_function inline bool
IsComputeTask
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0) && 
           (((id & TASK_ID_MASK_POOL_P ) >> TASK_ID_SHIFT_POOL ) == TASK_POOL_COMPUTE);
}

/// @summary Retrieve the work item for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the work item data.
internal_function inline COMPUTE_TASK_DATA*
GetTaskWorkItem
(
    task_id_t                 task,
    WIN32_TASK_SOURCE *source_list
)
{   // NOTE: this function does not check the validity of the task ID.
    uint32_t const thread_index = (task & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    uint32_t const buffer_index = (task & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[thread_index].WorkItems[buffer_index][task_index];
}

/// @summary Retrieve the work counter for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the work counter associated with the task.
internal_function inline int32_t*
GetTaskWorkCount
(
    task_id_t                 task,
    WIN32_TASK_SOURCE *source_list
)
{   // NOTE: this function does not check the validity of the task ID.
    uint32_t const thread_index = (task & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    uint32_t const buffer_index = (task & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[thread_index].WorkCounts[buffer_index][task_index];
}

/// @summary Retrieve the list of permits for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the permits list associated with the task.
internal_function inline PERMITS_LIST*
GetTaskPermitsList
(
    task_id_t                 task, 
    WIN32_TASK_SOURCE *source_list
)
{   // NOTE: this function does not check the validity of the task ID.
    uint32_t const thread_index = (task & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    uint32_t const buffer_index = (task & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[thread_index].PermitList[buffer_index][task_index];
}

/// @summary Push an item onto the private end of a task queue. This function can only be called by the thread that owns the queue, and may execute concurrently with one or more steal operations.
/// @param queue The queue to receive the item.
/// @param task The identifier of the task that is ready to run.
/// @return true if the task was written to the queue.
internal_function bool
ComputeTaskQueuePush
(
    COMPUTE_TASK_QUEUE *queue, 
    task_id_t            task
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
ComputeTaskQueueTake
(
    COMPUTE_TASK_QUEUE *queue
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
ComputeTaskQueueSteal
(
    COMPUTE_TASK_QUEUE *queue
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
internal_function void
ComputeTaskQueueClear
(
    COMPUTE_TASK_QUEUE *queue
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
internal_function void
CreateComputeTaskQueue
(
    COMPUTE_TASK_QUEUE   *queue, 
    size_t             capacity, 
    MEMORY_ARENA         *arena
)
{   // the capacity must be a power of two.
    assert((capacity & (capacity - 1)) == 0);
    queue->Public.store(0, std::memory_order_relaxed);
    queue->Private.store(0, std::memory_order_relaxed);
    queue->Mask  = int64_t(capacity) - 1;
    queue->Tasks = PushArray<task_id_t>(arena, capacity);
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

/// @summary Place an item in the concurrent work queue. Safe for multiple concurrent producers.
/// @param queue The concurrent work queue to write to.
/// @param task_id The task identifier.
/// @param task_main The task entry point.
/// @param task_data Optional work item-specific data to be copied into the work item.
/// @param data_size The size of the work item data, in bytes.
/// @return Zero if the item was enqueued, or -1 if the queue is currently full. 
internal_function int
GeneralTaskQueuePut
(
    GENERAL_TASK_QUEUE     *queue, 
    task_id_t             task_id, 
    GENERIC_ENTRYPOINT  task_main,
    void   const       *task_data, 
    size_t const        data_size
)
{
    GENERIC_TASK_DATA *item;
    uint32_t           mask = queue->Mask;
    uint32_t           tail = queue->Tail.load(std::memory_order_relaxed);

    for ( ; ; )
    {   // attempt to claim the slot at the tail of the queue.
        item  = &queue->WorkItems[tail & mask];
        uint32_t sequence = item->Sequence.load(std::memory_order_acquire);
        intptr_t     diff = intptr_t(sequence) - intptr_t(tail);

        if (diff == 0)
        {   // attempt to claim this slot in the queue storage.
            if (queue->Tail.compare_exchange_weak(tail, tail + 1, std::memory_order_relaxed))
                break; // got it.
        }
        else if (diff > 0)
        {   // lost the race to another producer; try again.
            tail = queue->Tail.load(std::memory_order_relaxed);
        }
        else // diff < 0
        {   // the queue is full; fail immediately.
            return -1;
        }
    }

    // set the work item data before the version tag.
    item->TaskId     = task_id;
    item->TaskMain   = task_main;
    if (data_size > 0) CopyMemory(item->Data, task_data, data_size);

    // update the sequence tag with the index of the next write slot.
    item->Sequence.store(tail + 1, std::memory_order_release);
    return 0;
}

/// @summary Attempt to retrieve a waiting item in a work queue. Safe for access by multiple concurrent consumers.
/// @param queue The work queue to read from.
/// @param dst On return, the dequeued work item data is copied to this location.
/// @return Zero if an item was dequeued, or -1 if the queue is currently empty.
internal_function int
GeneralTaskQueueGet
(
    GENERAL_TASK_QUEUE *queue, 
    GENERIC_TASK_DATA    *dst
)
{
    GENERIC_TASK_DATA  *item;
    uint32_t            mask = queue->Mask;
    uint32_t            head = queue->Head.load(std::memory_order_relaxed);

    for ( ; ; )
    {   // attempt to claim the item at the head of the queue.
        item  = &queue->WorkItems[head & mask];
        uint32_t sequence = item->Sequence.load(std::memory_order_acquire);
        intptr_t     diff = intptr_t(sequence) - intptr_t(head + 1);

        if (diff == 0)
        {   // attempt to claim this slot in the queue storage.
            if (queue->Head.compare_exchange_weak(head, head + 1, std::memory_order_relaxed))
                break; // got it.
        }
        else if (diff > 0)
        {   // lost the race to another consumer; try again.
            pos = queue->Head.load(std::memory_order_relaxed);
        }
        else // diff < 0
        {   // the queue is empty; fail immediately.
            return -1;
        }
    }

    CopyMemory(dst, item, sizeof(GENERIC_TASK_DATA));
    item->Sequence.store(head+mask+1, std::memory_order_release);
    return 0;
}

/// @summary Allocate the memory for a work queue and initialize the queue to empty.
/// @param queue The work queue to initialize.
/// @param capacity The capacity of the queue. This value must be a power of two greater than zero.
/// @param arena The memory arena to allocate from. The caller should ensure that sufficient memory is available.
internal_function void
CreateGeneralTaskQueue
(
    GENERAL_TASK_QUEUE   *queue, 
    size_t             capacity, 
    MEMORY_ARENA         *arena
)
{   // the capacity must be a power of two.
    assert((capacity & (capacity - 1)) == 0);
    queue->Tail.store(0, std::memory_order_relaxed);
    queue->Head.store(0, std::memory_order_relaxed);
    queue->Mask      = uint32_t(capacity) - 1;
    queue->WorkItems = PushArray<GENERIC_TASK_DATA>(arena, capacity);
    for (uint32_t  i = 0, n = uint32_t(capacity); i < n; ++i)
    {   // each item stores its position within the queue.
        queue->WorkItems[i].Sequence.store(i, std::memory_order_relaxed);
    }
}

/// @summary Calculate the amount of memory required to store a work queue.
/// @param capacity The capacity of the queue.
/// @return The minimum number of bytes required to store the queue items, not including the size of the GENERAL_TASK_QUEUE instance.
internal_function size_t
CalculateMemoryForGeneralTaskQueue
(
    size_t capacity
)
{
    return sizeof(GENERIC_TASK_DATA) * capacity;
}

/// @summary Calculate the amount of memory required to store compute task source data.
/// @param buffer_count The number of task buffers defined for the task source.
/// @param buffer_size The number of tasks per-buffer.
/// @return The minimum number of bytes required to store the task data, not including the size of the WIN32_TASK_SOURCE instance.
internal_function size_t
CalculateMemoryForTaskSource
(
    size_t buffer_count, 
    size_t  buffer_size 
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += CalculateMemoryForComputeTaskQueue(buffer_size);                       // WorkQueue
    size_in_bytes += AllocationSizeForArray<COMPUTE_TASK_DATA>(buffer_size) * buffer_count; // WorkItems
    size_in_bytes += AllocationSizeForArray<int32_t          >(buffer_size) * buffer_count; // WorkCounts
    size_in_bytes += AllocationSizeForArray<PERMITS_LIST     >(buffer_size) * buffer_count; // PermitList
    return bytes_needed;
}

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
        thread_pool->WorkerFreeList[thread_pool->WorkerFreeCount++] = uint32_t(i-1);
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

/// @summary Execute a compute task on the calling thread.
/// @param task The identifier of the task to execute.
/// @param worker_source The WIN32_TASK_SOURCE owned by the calling thread.
/// @param task_arena The memory arena to use for task-local memory allocations. The arena is reset prior to task execution.
/// @param thread_args Global data and state managed by the main thread.
/// @param scheduler The scheduler that owns the task.
internal_function void
ExecuteComputeTask
(
    task_id_t                     task,
    WIN32_TASK_SOURCE   *worker_source, 
    MEMORY_ARENA           *task_arena, 
    WIN32_THREAD_ARGS     *thread_args, 
    WIN32_TASK_SCHEDULER    *scheduler
)
{   
    COMPUTE_TASK_DATA *work_item;
    if (IsComputeTask(task) && (work_item = GetTaskWorkItem(task, worker_source->TaskSources)) != NULL)
    {   // TODO(rlk): Flip all pages of task_arena back to PAGE_READWRITE.
        ArenaReset(task_arena);
        work_item->TaskMain(task, worker_source, work_item, task_arena, thread_args, scheduler);
        // TODO(rlk): As a debugging feature, mark all pages of task_arena as PAGE_NOACCESS.
        FinishComputeTask(worker_source, task);
    }
}

/// @summary Execute a generic asynchronous task on the calling thread.
/// @param worker_source The WIN32_TASK_SOURCE owned by the calling thread.
/// @param work_item A thread-local copy of the task to execute.
/// @param task_arena The memory arena to use for task-local memory allocations. The arena is reset prior to task execution.
/// @param thread_args Global data and state managed by the main thread.
/// @param scheduler The scheduler that owns the task.
internal_function void
ExecuteGenericTask
(
    WIN32_TASK_SOURCE   *worker_source,
    GENERAL_TASK_DATA       *work_item,
    MEMORY_ARENA           *task_arena, 
    WIN32_THREAD_ARGS     *thread_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{   
    if (IsGeneralTask(work_item->TaskId))
    {   // TODO(rlk): Flip all pages of task_arena back to PAGE_READWRITE.
        ArenaReset(task_arena);
        work_item->TaskMain(work_item->TaskId, worker_source, work_item, task_arena, thread_args, scheduler);
        // TODO(rlk): As a debugging feature, mark all pages of task_arena as PAGE_NOACCESS.
    }
}

/// @summary Implements the entry point of an asynchronous task worker thread.
/// @param argp Pointer to the WIN32_WORKER_THREAD state associated with the thread.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
GenericWorkerMain
(
    void *argp
)
{
    WIN32_WORKER_THREAD *thread_args = (WIN32_WORKER_THREAD*)  argp;
    WIN32_THREAD_ARGS     *main_args =  thread_args->MainThreadArgs;
    HANDLE            init_signal[3] =
    { 
        thread_args->Signals.StartSignal,
        thread_args->Signals.ErrorSignal,
        thread_args->Signals.TerminateSignal
    };

    // TODO(rlk): any thread-local initialization.

    // signal to the scheduler that this worker thread is ready to go.
    SetEvent(thread_args->Signals.ReadySignal);

    // wait for a signal to start looking for work, or to terminate early.
    if (WaitForMultipleObjects(3, init_signal, FALSE, INFINITE) != WAIT_OBJECT_0)
    {   // either thread exit was signaled, or an error occurred while waiting.
        goto terminate_worker;
    }

    // TODO(rlk): the main loop of the generic task worker thread.
    // TODO(rlk): remember thread_args->StartData! it might contain a non-queued task to launch.
    // TODO(rlk): need a counting semaphore on the work queue.

terminate_worker:
    ConsoleOutput("Generic task worker thread %u terminated.\n", thread_args->WorkerIndex);
    return 0;
}

/// @summary Implements the entry point of an asynchronous task worker thread.
/// @param argp Pointer to the WIN32_WORKER_THREAD state associated with the thread.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
ComputeWorkerMain
(
    void *argp
)
{
    WIN32_WORKER_THREAD     *thread_args = (WIN32_WORKER_THREAD*)  argp;
    WIN32_THREAD_ARGS         *main_args =  thread_args->MainThreadArgs;
    WIN32_TASK_SOURCE     *worker_source =  thread_args->ThreadSource;
    MEMORY_ARENA           *worker_arena =  thread_args->ThreadArena; 
    COMPUTE_TASK_SOURCE *wait_source[64] = {};
    HANDLE               wait_signal[64] = {};
    HANDLE               init_signal[3]  =
    { 
        thread_args->Signals.StartSignal,
        thread_args->Signals.ErrorSignal,
        thread_args->Signals.TerminateSignal
    };

    // signal to the scheduler that this worker thread is ready to go.
    SetEvent(thread_args->Signals.ReadySignal);

    // wait for a signal to start looking for work, or to terminate early.
    if (WaitForMultipleObjects(3, init_signal, FALSE, INFINITE) != WAIT_OBJECT_0)
    {   // either thread exit was signaled, or an error occurred while waiting.
        goto terminate_worker;
    }

    // perform any initialization that must wait until all sources are prepared.
    wait_source[0]   = NULL;
    wait_signal[0]   = thread_args->HaltSignal;
    DWORD wait_count = BuildWorkerWaitList(&wait_signal[1], &wait_source[1], 63, worker_source);

    // this thread is the only thread that can put things into its work queue.
    // everything else is stolen work from other threads.
    for ( ; ; )
    {   // put the thread to sleep until there's potentially some work to steal.
        DWORD result  = WaitForMultipleObjects(wait_count, wait_signal, FALSE, INFINITE);
        if   (result >= WAIT_OBJECT_0 && result < (WAIT_OBJECT_0 + wait_count))
        {   // one of the events we were waiting on was signaled.
            if (result == WAIT_OBJECT_0)
            {   // the scheduler HALT signal woke us up. terminate the thread.
                goto terminate_worker;
            }
            // otherwise, this thread was woken because there's work to steal.
            task_id_t task  = ComputeTaskQueueSteal(&wait_source[result - WAIT_OBJECT_0]->WorkQueue);
            while    (task != INVALID_TASK_ID)
            {   // execute the task this thread just took or stole.
                ExecuteComputeTask(task, worker_source, &thread_args->ThreadArena, main_args);
                // that task may have generated additional work.
                // keep working as long as we can grab work to do.
                if ((task = ComputeTaskQueueTake(&worker_source->WorkQueue)) == INVALID_TASK_ID)
                {   // nothing left in the local queue, try to steal from the source that woke us.
                    task = ComputeTaskQueueSteal(&wait_source[result - WAIT_OBJECT_0]->WorkQueue);
                    // TODO(rlk): try and steal from a random worker?
                    // if task is INVALID_TASK_ID, this worker will go back to sleep.
                }
            }
        }
        else
        {   // some kind of error occurred while waiting. terminate the thread.
            goto terminate_worker;
        }
    }

terminate_worker:
    ConsoleOutput("Compute worker thread %zu terminated.\n", thread_args->ThreadIndex);
    return 0;
}

/// @summary Calculate the amount of memory required for a given scheduler configuration.
/// @param config The scheduler configuration returned from the CheckSchedulerConfiguration function.
/// @param max_sources The maximum number of task sources that can be registered with the scheduler.
/// @return The number of bytes required to create a compute task scheduler of the specified type with the given configuration.
internal_function size_t
CalculateMemoryForScheduler
(
    TASK_SCHEDULER_CONFIG     *config, 
)
{
    if (max_sources == 0)
    {   // default to the number of worker threads plus one for the main thread.
        max_sources = config->MaxWorkerThreads + 1;
    }
    if (max_sources > MAX_SCHEDULER_THREADS)
    {   // limit to the maximum number of sources.
        max_sources = MAX_SCHEDULER_THREADS;
    }

    size_t  size = 0;
    // account for the size of the variable-length data arrays.
    size += AllocationSizeForArray<unsigned int       >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<HANDLE             >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<COMPUTE_WORKER     >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<COMPUTE_TASK_SOURCE>(max_sources);
    // account for the size of the minimum number of task sources.
    for (size_t i = 0, n = config->MaxWorkerThreads + 1; i < n; ++i)
    {   // the main thread and worker threads always use the scheduler configuration.
        size += CalculateMemoryForComputeTaskSource(config->MaxActiveTicks, config->MaxTasksPerTick);
    }
    // ... 
    return size;
}

/// @summary Define a new task within a task source.
/// @param source The task source that will store the task definition.
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @param parent_id The identifier of the parent task, or INVALID_TASK_ID.
/// @param wait_task The identifier of the task that must complete prior to executing this new task, or INVALID_TASK_ID.
/// @return The identifier of the new task, or INVALID_TASK_ID.
internal_function task_id_t
DefineComputeTask
(
    WIN32_TASK_SOURCE           *source,
    COMPUTE_TASK_ENTRYPOINT   task_main, 
    void   const             *task_args, 
    size_t const              args_size, 
    task_id_t                 parent_id, 
    task_id_t                 wait_task
)
{
    if (args_size > COMPUTE_TASK_DATA::MAX_DATA)
    {   // there's too much data being passed. the caller should allocate storage elsewhere and pass us the pointer.
        ConsoleError("ERROR: Argument data too large when defining task (parent %08X). Passing %zu bytes, max is %zu bytes.\n", parent_id, args_size, COMPUTE_TASK::MAX_DATA); 
        return INVALID_TASK_ID;
    }
    if (source->TaskCount  == source->MaxTasksPerTick)
    {   // Bump the buffer index to the next buffer.
        source->TaskCount   = 0; // reset the task counter for the new "current" buffer.
        source->BufferIndex =(source->BufferIndex + 1) % source->MaxTicksInFlight;
        int32_t *work_count =&source->WorkCounts[source->BufferIndex][0];
        if (InterlockedAdd((volatile LONG*) work_count, 0) > 0)
        {   // Defining a new task would overwrite an active task. This check isn't thorough, but it's quick.
            ConsoleError("ERROR: Active task overwrite when defining task (parent %08X). Increase TASK_SOURCE MaxTasksPerTick.\n", parent_id);
            return INVALID_TASK_ID;
        }
    }

    int32_t     *dep_wcount;
    uint32_t   buffer_index = source->BufferIndex;
    uint32_t     task_index = source->TaskCount++;
    task_id_t       task_id = MakeTaskId(buffer_index, TASK_POOL_COMPUTE, task_index, source->SourceIndex);
    int32_t     &work_count = source->WorkCounts[buffer_index][task_index];
    COMPUTE_TASK &work_item = source->WorkItems [buffer_index][task_index];
    PERMITS_LIST    &permit = source->PermitList[buffer_index][task_index];

    work_item.TaskId        = task_id;
    work_item.ParentTask    = parent_id;
    work_item.TaskMain      = task_main;
    if (task_args != NULL  && args_size > 0)
    {   // we could first zero the work_item.TaskArgs memory.
        CopyMemory(work_item.Data, task_args, args_size);
    }
    permit.Count = 0; // this task doesn't have any dependents yet.
    work_count   = 2; // decremented in FinishComputeTask.

    if (IsComputeTask(wait_task) && (dep_wcount = GetTaskWorkCount(wait_task, source->TaskSources)) != NULL)
    {   // determine whether the dependency task has been completed.
        PERMITS_LIST *p = GetTaskPermitsList(wait_task, source->TaskSources);
        if (InterlockedAdd((volatile LONG*) dep_wcount, 0) > 0)
        {   // the dependency has not been completed, so update the permits list of the dependency.
            // the permits list for wait_task could be accessed concurrently by other threads:
            // - another thread could be executing DefineComputeTask with the same wait_task.
            // - another thread could be executing FinishComputeTask for wait_task.
            int32_t n;
            do
            {   // attempt to append the ID of the new task to the permits list of wait_task.
                if ((n = p->Count) == PERMITS_LIST::MAX_TASKS)
                {   // the best thing to do in this case is re-think your task breakdown.
                    ConsoleError("ERROR: Exceeded max permits on task %08X when defining task %08X (parent %08X).\n", wait_task, task_id, parent_id);
                    return task_id;
                }
                if (n < 0)
                {   // the wait_task completed during update of the permits list.
                    // this new task can be added to the ready-to-run queue.
                    ComputeTaskQueuePush(&source->WorkQueue, task_id);
                    return task_id;
                }
                // append the task ID to the permit list of wait_task.
                p->Tasks[n] = task_id;
                // and then try to update the number of permits.
            } while (InterlockedCompareExchange((volatile LONG*) &p->Count, n + 1, n) != n);

            return task_id;
        }
    }

    // this task is ready-to-run, so add it directly to the work queue.
    // it may be executed before LaunchTask is called, but that's ok - 
    // the work_count value is 2, so it cannot complete until LaunchTask.
    // this allows child tasks to be safely spawned from the main task.
    ComputeTaskQueuePush(&source->WorkQueue, task_id);
    return task_id;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Create a new semaphore object initialized with the specified resource count and spin count.
/// @param sem The semaphore object to initialize.
/// @param n The initial resource count of the semaphore.
/// @param spin_count The spin count.'
/// @return Zero if the semaphore is created successfully, or -1 if an error occurred.
public_function inline int
CreateSemaphore
(
    SEMAPHORE       *sem, 
    int32_t            n, 
    int32_t   spin_count
)
{
    sem->Count.N.store(n, std::memory_order_relaxed);
    sem->SpinCount = spin_count > 1 ? spin_count : 1;
    sem->KSem = CreateSemaphore(NULL, 0, LONG_MAX, NULL);
    return (sem->KSem != NULL) ? 0 : -1;
}

/// @summary Free resources associated with a semaphore object.
/// @param sem The semaphore to delete.
public_function inline void
DeleteSemaphore
(
    SEMAPHORE *sem
)
{
    if (sem->KSem != NULL)
    {
        CloseHandle(sem->KSem);
        sem->KSem = NULL;
    }
}

/// @summary Decrement a semaphore's internal counter and return whether or not a resource was available.
/// @param sem The semaphore to decrement.
/// @return true if a resource was available, or false if no resources are available.
public_function inline bool
TryWaitSemaphore
(
    SEMAPHORE *sem
)
{
    int32_t count = sem->Count.N.load(std::memory_order_acquire);
    while  (count > 0)
    {
        if (sem->Count.N.compare_exchange_weak(count, count - 1, std::memory_order_acq_rel, std::memory_order_relaxed))
            return true;
        // else, count was reloaded - optional backoff.
    }
    return false;
}

/// @summary Attempt to acquire a resource (decrement the semaphore's internal counter) and block the calling thread if none are available.
/// @param sem The semaphore to decrement.
public_function inline void
WaitSemaphoreNoSpin
(
    SEMAPHORE *sem
)
{
    if (sem->Count.N.fetch_add(-1, std::memory_order_acq_rel) < 1)
        WaitForSingleObject(sem->KSem, INFINITE);
}

/// @summary Attempt to acquire a resource (decrement the semaphore's internal counter) and block the calling thread if none are available.
/// @param sem The semaphore to decrement.
public_function void
WaitSemaphore
(
    SEMAPHORE *sem
)
{
    int32_t spin_count = sem->SpinCount;
    while  (spin_count--)
    {   // attempt to acquire the resource.
        int32_t count = sem->Count.N.load(std::memory_order_acquire);
        while  (count > 0)
        {
            if (sem->Count.N.compare_exchange_weak(count, count-1, std::memory_order_acq_rel, std::memory_order_relaxed))
                return; // successfully acquired a resource.
            // else, count was reloaded - optional backoff?
        }
    }
    // no additional spin cycles remaining; try one more time to acquire a resource and wait if none are available.
    if (sem->Count.N.fetch_add(-1, std::memory_order_acq_rel) < 1)
        WaitForSingleObject(sem->KSem, INFINITE);
}

/// @summary Make a resource available (increment the semaphore's internal counter) and unblock a single waiting thread.
/// @param sem The semaphore to increment.
public_function inline void
PostSemaphore
(
    SEMAPHORE *sem
)
{   // only signal the underlying semaphore of there was at least one thread waiting.
    if (sem->Count.N.fetch_add(1, std::memory_order_acq_rel) < 0)
        ReleaseSemaphore(sem->KSem, 1, 0);
}

/// @summary Make one or more resources available (increment the semaphore's internal counter) and unblock one or more waiting threads.
/// @param sem The semaphore to increment.
/// @param n The number of resources to make available.
public_function void
PostSemaphore
(
    SEMAPHORE *sem, 
    int32_t      n
)
{
    int32_t old = sem->Count.N.fetch_add(n, std::memory_order_acq_rel);
    if (old < 0)
    {
        int32_t min_waiters =-old;
        int32_t num_to_wake = num_waiters < n ? num_waiters : n; // min(num_waiters, n)
        ReleaseSemaphore(sem->KSem, num_to_wake, 0);
    }
}

/// @summary Retrieve the zero-based index of the thread that created a task.
/// @param id The task identifier.
/// @return The zero-based index of the thread that created the task.
public_function inline uint32_t
GetSourceThreadForTask
(
    task_id_t id
)
{
    return (id & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
}

/// @summary Extract all of the information from a task identifier.
/// @param parts The structure to populate with information extracted from the task identifier.
/// @param id The task identifier.
public_function inline void
GetTaskIdParts
(
    TASK_ID_PARTS *parts, 
    task_id_t         id
)
{
    parts->ValidTask     = (id & TASK_ID_MASK_VALID_P ) >> TASK_ID_SHIFT_VALID;
    parts->SchedulerType = (id & TASK_ID_MASK_TYPE_P  ) >> TASK_ID_SHIFT_TYPE;
    parts->ThreadIndex   = (id & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    parts->BufferIndex   = (id & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    parts->TaskIndex     = (id & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
}

/// @summary Validates a given scheduler configuration and populates any default values with their actual values. Emits performance warnings if necessary.
/// @param dst_config The configuration object to receive the validated configuration data.
/// @param src_config The configuration object specifying the input configuration data.
/// @param cpu_info Information about the CPU resources of the host system.
/// @param scheduler_type One of SCHEDULER_TYPE specifying the type of task scheduler being created.
/// @return true if no performance warnings were emitted.
public_function bool
CheckSchedulerConfiguration
(
    TASK_SCHEDULER_CONFIG    *dst_config, 
    TASK_SCHEDULER_CONFIG    *src_config, 
    WIN32_CPU_INFO             *cpu_info,
    uint32_t              scheduler_type
)
{
    if (dst_config == NULL)
    {   // a destination configuration object MUST be supplied.
        return false;
    }
    if (src_config == NULL)
    {   // an input configuration object MUST be supplied.
        return false;
    }
    if (cpu_info == NULL)
    {   // information about host CPU resources MUST be supplied.
        return false;
    }
    if (scheduler_type != SCHEDULER_TYPE_ASYNC && scheduler_type != SCHEDULER_TYPE_COMPUTE)
    {   // the input scheduler type is not valid.
        return false;
    }

    // we want to return a valid configuration, so track the source validation result.
    bool performance_ok = true;
    
    // default (if necessary) and validate the maximum number of ticks-in-flight.
    if (src_config->MaxActiveTicks == 0)
    {   // give the destination configuration the default value.
        dst_config->MaxActiveTicks  = 2;
    }
    else
    {   // copy the value from the input configuration.
        dst_config->MaxActiveTicks  = src_config->MaxActiveTicks;
    }
    // validate the MaxActiveTicks value.
    if (dst_config->MaxActiveTicks  > MAX_TICKS_IN_FLIGHT)
    {   // set to the largest acceptable value.
        ConsoleOutput("WARNING: Too many active ticks in-flight may cause excessive memory usage.\n");
        dst_config->MaxActiveTicks  = MAX_TICKS_IN_FLIGHT;
        performance_ok = false;
    }

    // default (if necessary) and validate the maximum number of worker threads.
    if (src_config->MaxWorkerThreads == 0)
    {   // the default value of this item depends on the scheduler type.
        if (scheduler_type == SCHEDULER_TYPE_ASYNC)
        {   // an async task scheduler is expected to have most threads idle/waiting.
            // therefore, allow it to have more worker threads total to handle additional requests.
            dst_config->MaxWorkerThreads = cpu_info->HardwareThreads * 2;
        }
        else if (scheduler_type == SCHEDULER_TYPE_COMPUTE)
        {   // a compute task scheduler shouldn't have more threads than there are hardware resources.
            dst_config->MaxWorkerThreads = cpu_info->HardwareThreads;
        }
        else
        {   // this case should have been caught by the check at the start of the function.
            ConsoleError("ERROR: Unhandled scheduler_type when defaulting TASK_SCHEDULER_CONFIG::MaxWorkerThreads. Defaulting to 1 thread.\n");
            dst_config->MaxWorkerThreads = 1;
            performance_ok = false;
        }
    }
    else
    {   // copy the value from the input configuration.
        dst_config->MaxWorkerThreads = src_config->MaxWorkerThreads;
    }
    // validate the MaxWorkerThreads value.
    if (dst_config->MaxWorkerThreads > MAX_WORKER_THREADS)
    {   // set to the largest acceptable value.
        ConsoleOutput("WARNING: Too many worker threads requested. An excessive number of worker threads may reduce performance.\n");
        dst_config->MaxWorkerThreads = MAX_WORKER_THREADS;
        performance_ok = false;
    }
    if (dst_config->MaxWorkerThreads <(cpu_info->HardwareThreads - 2))
    {   // spit out a warning in this case; the hardware is being under-utilized.
        ConsoleOutput("WARNING: Fewer worker threads than hardware threads requested; the hardware may be under-utilized.\n");
        performance_ok = false;
    }
    if (dst_config->MaxWorkerThreads >(cpu_info->HardwareThreads * 4))
    {   // spit out a warning in this case, which is probably hurting more than helping.
        ConsoleOutput("WARNING: Significantly more worker threads allowed than hardware resources available, which may decrease performance.\n");
        performance_ok = false;
    }

    // default (if necessary) and validate the maximum number of tasks per-tick.
    // this is really best set by the application, but use a reasonable default if none is specified.
    if (src_config->MaxTasksPerTick == 0)
    {   // the default value of this item depends on the scheduler type.
        if (scheduler_type == SCHEDULER_TYPE_ASYNC)
        {   // an async task scheduler will have far fewer tasks than a compute scheduler.
            dst_config->MaxTasksPerTick = 512;
        }
        else if (scheduler_type == SCHEDULER_TYPE_COMPUTE)
        {   // a compute task scheduler is expected to spawn many tasks.
            dst_config->MaxTasksPerTick = 4096;
        }
        else
        {   // this case should have been caught by the check at the start of the function.
            ConsoleError("ERROR: Unhandled scheduler_type when defaulting TASK_SCHEDULER_CONFIG::MaxTasksPerTick. Defaulting to 2048 tasks per-tick.\n");
            dst_config->MaxTasksPerTick = 2048;
            performance_ok = false;
        }
    }
    else
    {   // copy the value from the input configuration.
        dst_config->MaxTasksPerTick = src_config->MaxTasksPerTick;
    }
    // the maximum number of tasks per-tick should always be a power of two.
    if ((dst_config->MaxTasksPerTick & (dst_config->MaxTasksPerTick-1)) != 0)
    {   // round up to the next largest power-of-two.
        size_t n = 1;
        size_t m = dst_config->MaxTasksPerTick;
        while (n < m)
        {   // bump up to the next power-of-two.
            n <<= 1;
        }
        dst_config->MaxTasksPerTick = m;
    }
    // validate the MaxTasksPerTick value.
    if (dst_config->MaxTasksPerTick > MAX_TASKS_PER_TICK)
    {   // set to the largest acceptable value.
        ConsoleOutput("WARNING: Too many tasks per-tick will be spawned. Errors or excessive memory usage may result.\n");
        dst_config->MaxTasksPerTick = MAX_TASKS_PER_TICK;
        performance_ok = false;
    }

    // default (if necessary) and validate the maximum amount of worker thread-local memory.
    if (src_config->MaxTaskArenaSize == 0)
    {   // give each thread up to 2MB.
        dst_config->MaxTaskArenaSize = WORKER_THREAD_ARENA_SIZE_DEFAULT;
    }
    else
    {   // copy the value from the input configuration. this handles WORKER_THREAD_ARENA_NOT_NEEDED also.
        dst_config->MaxTaskArenaSize = src_config->MaxTaskArenaSize;
    }

    MEMORYSTATUSEX mem_info = {};
    mem_info.dwLength       = sizeof(MEMORYSTATUSEX);
    if (GlobalMemoryStatusEx(&mem_info) && (dst_config->MaxTaskArenaSize  != WORKER_THREAD_ARENA_NOT_NEEDED))
    {   // if the total amount of memory requested exceeds the amount of available physical memory, issue a warning.
        if (((DWORDLONG) dst_config->MaxWorkerThreads * (DWORDLONG) dst_config->MaxTaskArenaSize) >= mem_info.ullTotalPhys)
        {
            ConsoleOutput("WARNING: Total amount of task memory exceeds total physical memory.\n");
            performance_ok = false;
        }
    }

    return performance_ok;
}

/// @summary Allocate and initialize a TASK_SOURCE from a scheduler instance.
/// @param scheduler The scheduler instance that will monitor the work source.
/// @param arena The memory arena from which 'global' memory will be allocated.
/// @param buffer_size The maximum number of tasks per-buffer (the maximum number of tasks that the owner will launch per-tick.)
/// @return A pointer to the initialized WIN32_TASK_SOURCE, or NULL.
public_function WIN32_TASK_SOURCE*
NewTaskSource
(
    WIN32_TASK_SCHEDULER *scheduler, 
    MEMORY_ARENA             *arena, 
    size_t              buffer_size
)
{
    if ((buffer_size & (buffer_size - 1)) != 0)
    {   // this value must be a power-of-two. round up to the next multiple.
        size_t n = 1;
        size_t m = buffer_size;
        while (n < m)
        {
            n <<= 1;
        }
        buffer_size = n;
    }
    if (buffer_size > MAX_TASKS_PER_TICK)
    {   // consider this to be an error; it's easily trapped during development.
        return NULL;
    }
    if (scheduler->SourceCount == scheduler->MaxSourceCount)
    {   // no additional sources can be allocated from the scheduler.
        return NULL;
    }
    size_t bytes_needed = CalculateMemoryForTaskSource(2, buffer_size);
    size_t alignment    = std::alignment_of<void*>::value;
    if (!ArenaCanAllocate(arena, bytes_needed, alignment))
    {   // the arena doesn't have sufficient memory to initialize a source with the requested attributes.
        return NULL;
    }

    size_t const   PER_GROUP =  64;
    size_t        num_groups =((scheduler->MaxSourceCount - 1) / PER_GROUP) + 1;
    size_t         remaining =  scheduler->MaxSourceCount - (PER_GROUP * (num_groups - 1));
    size_t        last_group =  num_groups - 1;
    size_t        this_group =  scheduler->SourceCount / PER_GROUP;
    COMPUTE_TASK_SOURCE *src = &scheduler->SourceList[scheduler->SourceCount];
    src->StealSignal         = CreateEvent(NULL, FALSE, FALSE, NULL); // auto-reset
    src->GroupIndex          = this_group;
    src->SourceIndex         =(uint32_t)  scheduler->SourceCount;
    src->SourceGroupSize     =(uint32_t)((this_group == last_group) ? remaining : PER_GROUP);
    src->MaxTicksInFlight    = max_active_ticks;
    src->MaxTasksPerTick     = max_tasks_per_tick;
    src->TaskSourceCount     = scheduler->MaxSourceCount;
    src->TaskSources         = scheduler->SourceList;
    src->BufferIndex         = 0;
    src->TaskCount           = 0;
    CreateComputeTaskQueue(&src->WorkQueue , max_tasks_per_tick, arena);
    for (size_t i = 0, n = max_active_ticks; i < n; ++i)
    {
        src->WorkItems [i] = PushArray<COMPUTE_TASK>(arena, max_tasks_per_tick);
        src->WorkCounts[i] = PushArray<int32_t     >(arena, max_tasks_per_tick);
        src->PermitList[i] = PushArray<PERMITS_LIST>(arena, max_tasks_per_tick);
    }
    scheduler->SourceCount++;
    return src;
}

/// @summary Wake exactly one waiting worker thread if there's work it could steal. Call this if one or more items are added to a work queue.
/// @param source The WIN32_TASK_SOURCE owned by the calling thread.
public_function void
SignalWaitingWorkers
(
    WIN32_TASK_SOURCE *source
)
{   // TODO(rlk): should be a counting semaphore; call SignalWaitingWorkers(source, N).
    SetEvent(source->StealSignal);
}

/// @summary Create a new asynchronous task scheduler instance. The scheduler worker threads are launched separately.
/// @param scheduler The scheduler instance to initialize.
/// @param config The scheduler configuration.
/// @param main_args The global data managed by the main thread and passed to all worker threads.
/// @param arena The memory arena used to allocate scheduler memory. Per-worker memory is allocated directly from the OS.
/// @param max_sources The maximum number of task sources. The minimum value is the number of worker threads plus one for the main thread.
/// @return A pointer to the new scheduler instance, or NULL.
public_function int
CreateScheduler
(
    WIN32_TASK_SCHEDULER        *scheduler, 
    TASK_SCHEDULER_CONFIG const    *config, 
    MEMORY_ARENA                    *arena
)
{
    TASK_SCHEDULER_CONFIG valid_config = {};
    WIN32_CPU_INFO           *cpu_info = main_args->HostCPUInfo;
    size_t                   alignment = std::alignment_of<WIN32_COMPUTE_TASK_SCHEDULER>::value;

    CheckSchedulerConfiguration(&valid_config, config, cpu_info, SCHEDULER_TYPE_COMPUTE);
    if (max_sources == 0)
    {   // use the default, which is worker thread count + 1 for the main thread.
        max_sources = valid_config.MaxWorkerThreads + 1;
    }
    if (max_sources <(valid_config.MaxWorkerThreads + 1))
    {   // enforce a minimum value, each worker needs to be able to produce tasks.
        max_sources = valid_config.MaxWorkerThreads + 1;
    }
    if (max_sources > MAX_SCHEDULER_THREADS)
    {   // requesting more than the maximum supported number of sources is an error.
        ConsoleError("ERROR: Too many task sources requested (%u); max is %u.\n", (unsigned) max_sources, (unsigned) MAX_SCHEDULER_THREADS);
        return -1;
    }
    size_t expected_size = CalculateMemoryForComputeScheduler(&valid_config, max_sources);
    if (!ArenaCanAllocate(arena, expected_size, alignment))
    {   // there's not enough memory for the core scheduler and worker data.
        ConsoleError("ERROR: Insufficient memory in arena for task scheduler; need at least %zu bytes.\n", expected_size);
        return -1;
    }

    size_t mem_mark = ArenaMarker(arena);
    HANDLE ev_error = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE ev_start = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE ev_halt  = CreateEvent(NULL, TRUE, FALSE, NULL);

    scheduler->ErrorSignal      = ev_error;
    scheduler->StartSignal      = ev_start;
    scheduler->HaltSignal       = ev_halt;
    scheduler->ThreadCount      = 0;
    scheduler->OSThreadIds      = PushArray<unsigned int  >(arena, valid_config.MaxWorkerThreads);
    scheduler->OSThreadHandle   = PushArray<HANDLE        >(arena, valid_config.MaxWorkerThreads);
    scheduler->WorkerState      = PushArray<COMPUTE_WORKER>(arena, valid_config.MaxWorkerThreads);
    scheduler->MaxTicksInFlight = valid_config.MaxActiveTicks;
    scheduler->MaxTasksPerTick  = valid_config.MaxTasksPerTick;
    scheduler->MaxSourceCount   = max_sources;
    scheduler->SourceCount      = 0;
    scheduler->SourceList       = PushArray<COMPUTE_TASK_SOURCE>(arena, max_sources);
    ZeroMemory(scheduler->OSThreadIds   , valid_config.MaxWorkerThreads * sizeof(unsigned int));
    ZeroMemory(scheduler->OSThreadHandle, valid_config.MaxWorkerThreads * sizeof(HANDLE));
    ZeroMemory(scheduler->WorkerState   , valid_config.MaxWorkerThreads * sizeof(COMPUTE_WORKER));
    ZeroMemory(scheduler->SourceList    , max_sources                   * sizeof(COMPUTE_TASK_SOURCE));

    // always allocate source 0 to the main thread.
    NewComputeTaskSource(scheduler, arena, 0, 0);

    // spawn the worker threads. each thread is allocated a task source.
    size_t spawn_count = SCHEDULER_MIN(valid_config.MaxWorkerThreads, cpu_info->HardwareThreads);
    for (size_t i = 0; i < spawn_count; ++i)
    {
        if (!SpawnComputeWorker(&valid_config, scheduler, main_args, arena))
        {   // the worker thread could not be started, or failed to initialize.
            // there's no point in continuing.
            goto cleanup_and_fail;
        }
    }

    return 0;

cleanup_and_fail:
    if (scheduler != NULL && scheduler->ThreadCount > 0)
    {   // signal workers to die.
        SetEvent(ev_error); // all worker threads are waiting on this event.
        WaitForMultipleObjects((DWORD) scheduler->ThreadCount, scheduler->OSThreadHandle, TRUE, INFINITE);
    }
    if (ev_halt  != NULL) CloseHandle(ev_halt);
    if (ev_start != NULL) CloseHandle(ev_start);
    if (ev_error != NULL) CloseHandle(ev_error);
    ArenaResetToMarker(arena, mem_mark);
    return -1;
}

/// @summary Notify all task scheduler worker threads to start monitoring work queues.
/// @param scheduler The task scheduler to launch.
public_function void
LaunchScheduler
(
    WIN32_TASK_SCHEDULER *scheduler
)
{
    SetEvent(scheduler->StartSignal);
}

/// @summary Notify all task scheduler worker threads to shutdown.
/// @param scheduler The task scheduler to halt.
public_function void
HaltScheduler
(
    WIN32_TASK_SCHEDULER *scheduler
)
{
    // signal all worker threads to exit, and wait for them.
    SetEvent(scheduler->TerminateSignal);
    // TODO(rlk): this is done differently now because of the separate thread pools.
}

/// @summary Retrieve the task source for the root thread. This function should only ever be called from the root thread (the thread that submits the root tasks.)
/// @param scheduler The scheduler instance to query.
/// @return A pointer to the worker state for the root thread, which can be used to spawn root tasks.
public_function inline WIN32_TASK_SOURCE*
RootTaskSource
(
    WIN32_TASK_SCHEDULER *scheduler
)
{
    return &scheduler->SourceList[0];
}

/// @summary Spawn a new task. After spawning any child tasks, call FinishComputeTask to complete the task definition.
/// @param source The WIN32_TASK_SOURCE owned by the calling thread. 
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @param wait_task The identifier of the task that must complete prior to executing this new task, or INVALID_TASK_ID.
/// @return The identifier of the new task, or INVALID_TASK_ID.
public_function inline task_id_t
NewComputeTask
(
    WIN32_TASK_SOURCE         *source, 
    COMPUTE_TASK_ENTRYPOINT task_main, 
    void   const           *task_args, 
    size_t const            args_size,
    task_id_t               wait_task = INVALID_TASK_ID
)
{
    return DefineComputeTask(source, task_main, task_args, args_size, INVALID_TASK_ID, wait_task);
}

/// @summary Spawn a new child task. Call FinishComputeTask to complete the task definition.
/// @param source The WIN32_TASK_SOURCE owned by the calling thread. 
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @param parent_id The task ID of the parent task, as returned by the NewTask function used to create the parent task.
/// @param wait_task The identifier of the task that must complete prior to executing this new task, or INVALID_TASK_ID.
/// @return The identifier of the new task, or INVALID_TASK_ID.
public_function task_id_t
NewChildTask
(
    WIN32_TASK_SOURCE         *source, 
    COMPUTE_TASK_ENTRYPOINT task_main, 
    void   const           *task_args, 
    size_t const            args_size, 
    task_id_t               parent_id, 
    task_id_t               wait_task = INVALID_TASK_ID
)
{
    COMPUTE_TASK_DATA *parent_task;  // read-only
    int32_t           *parent_count; // write-only
    if (IsComputeTask(parent_id))
    {   // retrieve the information we need about the parent task.
        parent_task  = GetTaskWorkItem (parent_id, source->TaskSources);
        parent_count = GetTaskWorkCount(parent_id, source->TaskSources);
        // increment the outstanding work counter on the parent.
        InterlockedIncrement((volatile LONG*) parent_count);
    }
    else
    {   // the parent ID isn't valid, so fail the child creation.
        return INVALID_TASK_ID;
    }

    return DefineComputeTask(source, task_main, task_args, args_size, parent_id, wait_task);
}

/// @summary Indicate that a task (including any children) has been completely defined and allow it to finish execution.
/// @param source The source that defined the task. This should be the same source that was passed to NewComputeTask.
/// @param task The task ID returned by the NewComputeTask call.
/// @return The number of tasks added to the work queue of the calling thread (made ready-to-run by completion of task.)
public_function int32_t
FinishComputeTask
(
    WIN32_TASK_SOURCE *source,
    task_id_t            task
)
{
    int32_t *work_count;
    if (IsComputeTask(task) && (work_count = GetTaskWorkCount(task, source->TaskSources)) != NULL)
    {
        COMPUTE_TASK_DATA *work_item = GetTaskWorkItem(task, source->TaskSources);
        if (InterlockedDecrement((volatile LONG*) work_count) <= 0)
        {   // the work item has finished executing. this may permit other tasks to run.
            PERMITS_LIST *p = GetTaskPermitsList(task, source->TaskSources);
            int32_t n = InterlockedExchange((volatile LONG*) &p->Count, -1);
            if (n > 0)
            {   // add the now-permitted tasks to the work queue of the calling thread.
                for (int32_t i = 0; i < n; ++i)
                {
                    ComputeTaskQueuePush(&source->WorkQueue, p->Tasks[i]);
                }
            }
            // decrement the work counter on any parent task.
            if ((n += FinishComputeTask(source, work_item->ParentTask)) > 0)
            {   // wake up idle threads to help work.
                SignalWaitingWorkers(source);
            }
            return n;
        }
    }
    return 0;
}

