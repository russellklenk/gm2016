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
    WORKER_FLAGS_NONE        = (0UL << 0),       /// The worker thread has the default behavior.
    WORKER_FLAGS_GENERAL     = (1UL << 0),       /// The worker thread can execute general tasks.
    WORKER_FLAGS_COMPUTE     = (1UL << 1),       /// The worker thread can execute compute tasks.
};

/// @summary Define identifiers for task ID validity. An ID can only be valid or invalid.
enum TASK_ID_TYPE : uint32_t
{
    TASK_ID_TYPE_INVALID     = 0,                /// The task identifier specifies an invalid task.
    TASK_ID_TYPE_VALID       = 1,                /// The task identifier specifies a valid task.
};

/// @summary Define identifiers for supported task pools. A task is either blocking or non-blocking.
enum TASK_POOL : uint32_t
{
    TASK_POOL_COMPUTE        = 0,                /// The task executes on the compute thread pool, designed for non-blocking, work-heavy tasks.
    TASK_POOL_GENERAL        = 1,                /// The task executes on the general thread pool, designed for blocking, light-CPU tasks.
    TASK_POOL_COUNT          = 2                 /// The number of thread pools defined by the scheduler.
};

/// @summary Define the data associated with a spinning semaphore, which is guaranteed to remain in user mode unless a thread must be put to sleep or woken.
struct SEMAPHORE
{   static size_t const      PAD                   = CACHELINE_SIZE - sizeof(std::atomic<int32_t>);
    std::atomic<int32_t>     Count;              /// The current count of the semaphore.
    uint8_t                  Padding[PAD];       /// Unused padding out to the next cacheline boundary.
    int32_t                  SpinCount;          /// The spin count assigned to the semaphore at creation time.
    HANDLE                   KSem;               /// The handle of the kernel semaphore object.
};

/// @summary Define a structure specifying the constituent parts of a task ID.
struct TASK_ID_PARTS
{
    uint32_t                 ValidTask;          /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t                 PoolType;           /// One of TASK_POOL specifying the thread pool that executes the task.
    uint32_t                 ThreadIndex;        /// The zero-based index of the thread that defines the task.
    uint32_t                 BufferIndex;        /// The zero-based index of the thread-local buffer that defines the task.
    uint32_t                 TaskIndex;          /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define the data associated with a task that runs on the general thread pool.
/// These tasks are expected to have a light CPU work load, and may perform blocking, long-running operations.
struct GENERAL_TASK_DATA
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      MAX_DATA              = 48;
    std::atomic<uint32_t>    Sequence;           /// A sequence value assigned by the GENERAL_TASK_QUEUE.
    task_id_t                TaskId;             /// The task identifier.
    GENERAL_TASK_ENTRYPOINT  TaskMain;           /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t                 Reserved1;          /// Padding; unused.
#endif
    uint8_t                  Data[MAX_DATA];     /// User-supplied argument data associated with the work item.
};

/// @summary Define the data associated with a task that runs on the compute thread pool.
/// These tasks are expected to be CPU-heavy and not perform any blocking or long-running operations.
struct COMPUTE_TASK_DATA
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      MAX_DATA              = 48;
    task_id_t                TaskId;             /// The task identifier.
    task_id_t                ParentTask;         /// The identifier of the parent task, or INVALID_TASK_ID.
    COMPUTE_TASK_ENTRYPOINT  TaskMain;           /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t                 Reserved1;          /// Padding; unused.
#endif
    uint8_t                  Data[MAX_DATA];     /// User-supplied argument data associated with the work item.
};

/// @summary Define the data representing a work queue safe for access by multiple concurrent producers and multiple concurrent consumers.
/// The work item data is stored directly within the queue. All tasks in the queue are in the ready-to-run state. A single queue feeds all workers.
struct GENERAL_TASK_QUEUE
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      PAD                   = CACHELINE_SIZE - sizeof(std::atomic<uint32_t>);
    std::atomic<uint32_t>    Tail;               /// The index at which items are enqueued, representing the tail of the queue.
    uint8_t                  Pad0[PAD];          /// Padding separating producer data from consumer data.
    std::atomic<uint32_t>    Head;               /// The index at which items are dequeued, representing the head of the queue.
    uint8_t                  Pad1[PAD];          /// Padding separating consumer data from shared data.
    uint32_t                 Mask;               /// The bitmask used to map the Head and Tail indices into the storage array.
    GENERAL_TASK_DATA       *WorkItems;          /// The data associated with each ready-to-run work item.
};

/// @summary Define the data representing a work-stealing deque of task identifiers. Each compute worker thread maintains its own queue.
/// The worker thread can perform push and take operations. Other worker threads can perform concurrent steal operations.
struct COMPUTE_TASK_QUEUE
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      PAD                   = CACHELINE_SIZE - sizeof(std::atomic<int64_t>);
    std::atomic<int64_t>     Public;             /// The public end of the deque, updated by steal operations (Top).
    uint8_t                  Pad0[PAD];          /// Padding separating the public data from the private data.
    std::atomic<int64_t>     Private;            /// The private end of the deque, updated by push and take operations (Bottom).
    uint8_t                  Pad1[PAD];          /// Padding separating the private data from the storage data.
    int64_t                  Mask;               /// The bitmask used to map the Top and Bottom indices into the storage array.
    task_id_t               *Tasks;              /// The identifiers for the tasks in the queue.
};

/// @summary Defines the data associated with a set of tasks waiting on another task to complete.
/// Only tasks in the compute queue support dependencies and permits.
struct PERMITS_LIST
{   static size_t const      ALIGNMENT             = CACHELINE_SIZE;
    static size_t const      MAX_TASKS             = 15;
    std::atomic<int32_t>     Count;              /// The number of items in the permits list.
    task_id_t                Tasks[MAX_TASKS];   /// The task IDs in the permits list. This is the set of tasks to launch when the owning task completes.
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
    HANDLE                   StartSignal;        /// Signal set by the coordinator to allow all active worker threads to start running.
    HANDLE                   ErrorSignal;        /// Signal set by any thread to indicate that a fatal error has occurred.
    HANDLE                   TerminateSignal;    /// Signal set by the coordinator to stop all active worker threads.
    uint32_t                 WorkerFlags;        /// The WORKER_FLAGS to apply to worker threads in the pool.
};

/// @summary Define the data associated with a thread that can produce compute tasks (but not necessarily execute them.)
/// Each worker thread in the scheduler thread pool is a COMPUTE_TASK_SOURCE that can also execute tasks.
/// The maximum number of task sources is fixed at scheduler creation time.
struct cacheline_align WIN32_TASK_SOURCE
{   static size_t const      ALIGNMENT              = CACHELINE_SIZE;

    COMPUTE_TASK_QUEUE       ComputeWorkQueue;   /// The queue of ready-to-run task IDs in the compute pool.
    SEMAPHORE                StealSignal;        /// A counting semaphore used to wake waiting threads when work can be stolen.

    GENERAL_TASK_QUEUE      *GeneralWorkQueue;   /// The MPMC queue of general task data.
    size_t                   SourceIndex;        /// The zero-based index of the source within the source list.
    size_t                   SourceCount;        /// The total number of task sources defined in the scheduler. Constant.
    WIN32_TASK_SOURCE       *TaskSources;        /// The list of per-source state for each task source. Managed by the scheduler.
    
    uint32_t                 TasksPerBuffer;     /// The allocation capacity of a single task buffer.
    uint32_t                 BufferIndex;        /// The zero-based index of the task buffer being written to.
    uint32_t                 ComputeTaskCount;   /// The zero-based index of the next available task in the current compute task buffer.
    uint32_t                 GeneralTaskCount;   /// The zero-based index of the next available task in the general queue.

    COMPUTE_TASK_DATA       *WorkItems [2];      /// The work item definitions for each compute task, for each buffer.
    int32_t                 *WorkCounts[2];      /// The outstanding work counter for each compute task, for each buffer.
    PERMITS_LIST            *PermitList[2];      /// The permits list for each compute task, for each buffer.
};

/// @summary Define the data associated with a compute-oriented task scheduler.
struct WIN32_TASK_SCHEDULER
{
    size_t                   MaxSources;         /// The maximum number of compute task sources.
    size_t                   SourceCount;        /// The number of allocated compute task sources.
    WIN32_TASK_SOURCE       *SourceList;         /// The data associated with each compute task source. SourceCount are currently valid.

    WIN32_THREAD_POOL        GeneralPool;        /// The thread pool used for running light-work asynchronous tasks.
    WIN32_THREAD_POOL        ComputePool;        /// The thread pool used for running work-heavy, non-blocking tasks.

    GENERAL_TASK_QUEUE       GeneralWorkQueue;   /// The MPMC queue of tasks to execute in the general pool.
    SEMAPHORE                GeneralWorkSignal;  /// A counting semaphore used to wake waiting threads when general tasks are waiting.

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
    WIN32_THREAD_ARGS       *MainThreadArgs;     /// The global data managed by the main thread and available to all threads.
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
/// @return Zero if the queue is created successfully, or -1 if an error occurred.
internal_function int
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
/// @return Zero if the queue was created successfully, or -1 if an error occurred. 
internal_function int
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
    return 0;
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
/// @return Zero if the worker thread was successfully started, or -1 if an error occurred.
internal_function int
SpawnWorkerThread
(
    WIN32_THREAD_POOL *thread_pool, 
    uint32_t            pool_index,
    uint32_t          worker_flags
)
{
    if (pool_index >= thread_pool->MaxThreads)
    {   // an invalid pool index was specified. this is a silent error.
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
        ConsoleError("ERROR (%s): Creation of thread ready event failed (%08X).\n", GetLastError());
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
        ConsoleError("ERROR (%s): Thread creation failed (errno = %d).\n", errno);
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
    WIN32_THREAD_POOL   *thread_pool =  tnread_args->ThreadPool;
    WIN32_TASK_SCHEDULER  *scheduler =  thread_args->TaskScheduler;
    WIN32_TASK_SOURCE *worker_source =  thread_args->ThreadSource;
    MEMORY_ARENA       *worker_arena =  thread_args->ThreadArena; 
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

    // TODO(rlk): need a counting semaphore on the work queue.
    // scheduler->GeneralWorkSignal is a counting semaphore, but not suitable for use with WFMO.
    // probably best to make it just a regular kernel semaphore.

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
    WIN32_TASK_SOURCE          *source,
    COMPUTE_TASK_ENTRYPOINT  task_main, 
    void   const            *task_args, 
    size_t const             args_size, 
    task_id_t                parent_id, 
    task_id_t                wait_task
)
{
    if (args_size > COMPUTE_TASK_DATA::MAX_DATA)
    {   // there's too much data being passed. the caller should allocate storage elsewhere and pass us the pointer.
        ConsoleError("ERROR (%s): Argument data too large when defining task (parent %08X). Passing %zu bytes, max is %zu bytes.\n", __FUNCTION__, parent_id, args_size, COMPUTE_TASK::MAX_DATA); 
        return INVALID_TASK_ID;
    }
    if (source->ComputeTaskCount == source->TasksPerBuffer)
    {   // bump the buffer index to the next buffer.
        source->ComputeTaskCount  = 0;
        source->BufferIndex = (source->BufferIndex + 1) & 1; // % 2
        int32_t *work_count = &source->WorkCounts[source->BufferIndex][0];
        if (InterlockedAdd((volatile LONG*) work_count, 0) > 0)
        {   // Defining a new task would overwrite an active task. This check isn't thorough, but it's quick.
            ConsoleError("ERROR (%s): Active task overwrite when defining task (parent %08X). Increase WIN32_TASK_SOURCE::TasksPerBuffer.\n", __FUNCTION__, parent_id);
            return INVALID_TASK_ID;
        }
    }

    int32_t     *dep_wcount;
    uint32_t   buffer_index = source->BufferIndex;
    uint32_t     task_index = source->ComputeTaskCount++;
    task_id_t       task_id = MakeTaskId(buffer_index, TASK_POOL_COMPUTE, task_index, source->SourceIndex);
    int32_t     &work_count = source->WorkCounts[buffer_index][task_index];
    COMPUTE_TASK_DATA &task = source->WorkItems [buffer_index][task_index];
    PERMITS_LIST    &permit = source->PermitList[buffer_index][task_index];

    task.TaskId     = task_id;
    task.ParentTask = parent_id;
    task.TaskMain   = task_main;
    if (task_args  != NULL && args_size > 0)
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
                    ConsoleError("ERROR (%s): Exceeded max permits on task %08X when defining task %08X (parent %08X).\n", __FUNCTION__, wait_task, task_id, parent_id);
                    return task_id;
                }
                if (n < 0)
                {   // the wait_task completed during update of the permits list.
                    // this new task can be added to the ready-to-run queue.
                    ComputeTaskQueuePush(&source->ComputeWorkQueue, task_id);
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
    ComputeTaskQueuePush(&source->ComputeWorkQueue, task_id);
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
        ConsoleError("ERROR (%s): Insufficient memory to initialize thread pool; need %zu bytes.\n", __FUNCTION__, mem_required);
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
    thread_pool->WorkerFlags              = pool_config->WorkerFlags;
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

    // initialize the array of pointers to TASK_SOURCE objects.
    // the pointers point back into the scheduler's SourceList.
    for (size_t i = 0; i < pool_config->MaxThreads; ++i)
    {
        thread_pool->WorkerSource[i] = &scheduler->SourceList[pool_config->WorkerSourceIndex + i];
    }

    // spawn workers until the maximum thread count is reached.
    // it might be nice to re-introduce the concept of transient threads, which 
    // die after some period of idle-ness, but that may not be work the implementation complexity.
    // idle workers will just sit in a wait state until there's work for them to do.
    for (size_t i = 0; i < pool_config->MaxThreads; ++i)
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

/// @summary Signal all threads in a thread pool to terminate, and block the calling thread until all workers have exited.
/// @param thread_pool The thread pool to terminate.
public_function void
TerminateThreadPool
(
    WIN32_THREAD_POOL *thread_pool
)
{
    if (thread_pool->TerminateSignal != NULL)
    {
        DWORD n = (DWORD) thread_pool->ActiveThreads;
        // signal all threads in the pool to terminate. this may take some time
        // if the worker threads are currently executing blocking tasks.
        SetEvent(thread_pool->TerminateSignal);
        // block the calling thread until all threads in the pool have terminated.
        WaitForMultipleObjects(n, thread_pool->OSThreadHandle, TRUE, INFINITE);
    }
}

/// @summary Calculate the amount of memory required for a given scheduler configuration.
/// @param config The pre-validated scheduler configuration.
/// @return The number of bytes required to create a compute task scheduler of the specified type with the given configuration.
public_function size_t
CalculateMemoryForScheduler
(
    WIN32_TASK_SCHEDULER_CONFIG const *config, 
)
{
    size_t    buffer_count = 2;
    size_t   size_in_bytes = 0;
    size_t general_threads = config->PoolSize[TASK_POOL_GENERAL].MaxThreads;
    size_t compute_threads = config->PoolSize[TASK_POOL_COMPUTE].MaxThreads;
    size_t   total_threads = general_threads + compute_threads;
    size_t   general_tasks = config->PoolSize[TASK_POOL_GENERAL].MaxTasks;
    size_t   compute_tasks = config->PoolSize[TASK_POOL_COMPUTE].MaxTasks;
    size_t   max_max_tasks = general_tasks > compute_tasks ? general_tasks : compute_tasks;
    // account for the size of the thread pools.
    size_in_bytes += CalculateMemoryForThreadPool(general_threads);
    size_in_bytes += CalculateMemoryForThreadPool(compute_threads);
    // account for the size of the main thread task source.
    size_in_bytes += CalculateMemoryForTaskSource(buffer_count, max_max_tasks);
    // account for the size of the general pool worker threads.
    size_in_bytes += CalculateMemoryForTaskSource(buffer_count, general_tasks) * general_threads;
    // account for the size of the compute pool worker threads.
    size_in_bytes += CalculateMemoryForTaskSource(buffer_count, compute_tasks) * compute_threads;
    // ... 
    return size_in_bytes;
}

/// @summary Retrieve a default scheduler configuration based on host CPU resources.
/// @param config The scheduler configuration to populate.
/// @param host_cpu_info Information about the CPU resources of the host system.
public_function void
DefaultSchedulerConfiguration
(
    WIN32_TASK_SCHEDULER_CONFIG        *config, 
    WIN32_CPU_INFO const        *host_cpu_info
)
{   // everything starts out as zero.
    ZeroMemory(config, sizeof(WIN32_TASK_SCHEDULER_CONFIG));
    // set up defaults for the general pool. tasks are expected to be created relatively 
    // infrequently, and to be fairly long-running (one to several application ticks.) 
    // having more software threads than hardware threads helps reduce latency.
    WIN32_THREAD_POOL_SIZE &general_pool = config->PoolSize[TASK_POOL_GENERAL];
    general_pool.MinThreads = 2;
    general_pool.MaxThreads = host_cpu_info->HardwareThreads * 2;
    general_pool.MaxTasks   = 256;
    general_pool.ArenaSize  = 8 * 1024 * 1024; // 8MB
    // set up defaults for the compute pool. tasks are expected to be created very 
    // frequently, and to be short lived (less than one application tick.) limit 
    // to the number of hardware threads to avoid over-subscribing CPU resources.
    WIN32_THREAD_POOL_SIZE &compute_pool = config->PoolSize[TASK_POOL_COMPUTE];
    compute_pool.MinThreads = 1;
    compute_pool.MaxThreads = host_cpu_info->HardwareThreads - 1;
    compute_pool.MaxTasks   = 2048;
    compute_pool.ArenaSize  = 2 * 1024 * 1024;  // 2MB
    if (compute_pool.MaxThreads < 1)
    {   // on a single-core system, limit to 1 thread.
        compute_pool.MaxThreads = 1;
    }
    // scale up the maximum number of tasks per-pool based on hardware resources.
    if (host_cpu_info->HardwareThreads > 8)
    {   // six-core CPU or greater - allow more tasks to be created.
        general_pool.MaxTasks = 512;
        compute_pool.MaxTasks = 4096;
    }
    if (host_cpu_info->HardwareThreads > 16)
    {   // more than six cores, allow even more tasks to be created.
        general_pool.MaxTasks = 1024;
        compute_pool.MaxTasks = 8192;
    }
    if (host_cpu_info->HardwareThreads > 32)
    {   // and so on...
        general_pool.MaxTasks = 2048;
        compute_pool.MaxTasks = 16384;
    }
    if (host_cpu_info->HardwareThreads > 64)
    {   // and so on...
        general_pool.MaxTasks = 4096;
        compute_pool.MaxTasks = 32768;
    }
    if (host_cpu_info->HardwareThreads > 128)
    {   // and so on.
        general_pool.MaxTasks = 8192;
        compute_pool.MaxTasks = 65536;
    }
    // default the maximum number of task sources to the number of worker threads + 1 for the main thread.
    config->MaxTaskSources = general_pool.MaxThreads + compute_pool.MaxThreads + 1;
}

/// @summary Validates a given scheduler configuration.
/// @param dst_config The configuration object to receive the validated configuration data.
/// @param src_config The configuration object specifying the input configuration data.
/// @param performance_warnings On return, set to true if one or more performance warnings were emitted.
/// @return Zero if the configuration is valid, or -1 if the configuration is not valid.
public_function int
CheckSchedulerConfiguration
(
    WIN32_TASK_SCHEDULER_CONFIG       *dst_config, 
    WIN32_TASK_SCHEDULER_CONFIG const *src_config,
    bool                    &performance_warnings
)
{
    if (dst_config == NULL)
    {
        ConsoleError("ERROR (%s): A destination scheduler configuration must be supplied.\n", __FUNCTION__);
        performance_warnings = false;
        return -1;
    }
    if (src_config == NULL)
    {
        ConsoleError("ERROR (%s): A source scheduler configuration must be supplied.\n", __FUNCTION__);
        performance_warnings = false;
        return -1;
    }
    if (src_config->MainThreadArgs == NULL || src_config->MainThreadArgs->HostCPUInfo == NULL)
    {
        ConsoleError("ERROR (%s): Host CPU information must be supplied to on WIN32_TASK_SCHEDULER_CONFIG::MainThreadArgs.\n", __FUNCTION__);
        performance_warnings = false;
        return -1;
    }

    // retrieve the current system memory usage.
    MEMORYSTATUSEX memory = {};
    size_t general_memory = 0;
    size_t compute_memory = 0;
    ZeroMemory(&memory, sizeof(MEMORYSTATUSEX));
    memory.dwLength   = sizeof(MEMORYSTATUSEX);
    GlobalMemoryStatusEx(&memory);

    {   // validation for the general thread pool.
        // the general thread pool jobs are expected to be created relatively infrequently,
        // and each job is expected to be relatively long-lived. having more threads than 
        // hardware threads helps to keep CPU resources busy when there's no compute work.
        WIN32_THREAD_POOL_SIZE const &src = src_config->PoolSize[TASK_POOL_GENERAL];
        WIN32_THREAD_POOL_SIZE       &dst = dst_config->PoolSize[TASK_POOL_GENERAL];

        if (src.MinThreads < 1)
        {   // the general pool must have at least one background thread.
            dst.MinThreads = 1;
        }
        else
        {   // copy the value from the source configuration.
            dst.MinThreads = src.MinThreads;
        }
        if (src.MaxThreads < 1)
        {   // the general pool must have at least one background thread.
            dst.MaxThreads = 1;
        }
        else
        {   // copy the value from the source configuration.
            dst.MaxThreads = src.MaxThreads;
        }
        if (dst.MinThreads > dst.MaxThreads)
        {   // swap to ensure that max >= min.
            size_t    temp = dst.MinThreads;
            dst.MinThreads = dst.MaxThreads;
            dst.MaxThreads = dst.MinThreads;
        }
        if (src.MaxTasks <= (dst.MaxThreads * 4))
        {   // select a more reasonable value based on available hardware resources.
            if (host_cpu_info->HardwareThreads <   8) dst.MaxTasks = 256;
            if (host_cpu_info->HardwareThreads >   8) dst.MaxTasks = 512;
            if (host_cpu_info->HardwareThreads >  16) dst.MaxTasks = 1024;
            if (host_cpu_info->HardwareThreads >  32) dst.MaxTasks = 2048;
            if (host_cpu_info->HardwareThreads >  64) dst.MaxTasks = 4096;
            if (host_cpu_info->HardwareThreads > 128) dst.MaxTasks = 8192;
        }
        else
        {   // copy the value from the source configuration.
            dst.MaxTasks = src.MaxTasks;
        }
        // the maximum number of tasks per-tick should always be a power of two.
        if ((dst.MaxTasks & (dst.MaxTasks-1)) != 0)
        {   // round up to the next largest power-of-two.
            size_t n = 1;
            size_t m = dst.MaxTasks;
            while (n < m)
            {   // bump up to the next power-of-two.
                n <<= 1;
            }
            dst.MaxTasks = m;
        }
        // there is no per-thread memory requirement, so copy the source value directly.
        // calculate the amount of per-thread memory used in the general pool.
        dst.ArenaSize   = src.ArenaSize;
        general_memory  = dst.MaxThreads * dst.ArenaSize;
        general_memory += dst.MaxThreads * CalculateMemoryForTaskSource(2, dst.MaxTasks);
    }
    {   // validation for the compute thread pool.
        // the compute thread pool jobs are expected to be created very frequently, in large 
        // numbers, and each job is expected to be very short-lived (one tick or less.)
        // to avoid over-subscribing CPU resources, limit to the number of hardware threads.
        WIN32_THREAD_POOL_SIZE const &src = src_config->PoolSize[TASK_POOL_COMPUTE];
        WIN32_THREAD_POOL_SIZE       &dst = dst_config->PoolSize[TASK_POOL_COMPUTE];

        if (src.MinThreads < 1)
        {   // the compute pool must have at least one background thread.
            dst.MinThreads = 1;
        }
        else
        {   // copy the value from the source configuration.
            dst.MinThreads = src.MinThreads;
        }
        if (src.MaxThreads < 1)
        {   // the compute pool must have at least one background thread.
            dst.MaxThreads = 1;
        }
        else
        {   // copy the value from the source configuration.
            dst.MaxThreads = src.MaxThreads;
        }
        if (dst.MinThreads > dst.MaxThreads)
        {   // swap to ensure that max >= min.
            size_t    temp = dst.MinThreads;
            dst.MinThreads = dst.MaxThreads;
            dst.MaxThreads = dst.MinThreads;
        }
        if (src.MaxTasks <= (dst.MaxThreads * 64))
        {   // select a more reasonable value based on available hardware resources.
            if (host_cpu_info->HardwareThreads <   8) dst.MaxTasks = 2048;
            if (host_cpu_info->HardwareThreads >   8) dst.MaxTasks = 4096;
            if (host_cpu_info->HardwareThreads >  16) dst.MaxTasks = 8192;
            if (host_cpu_info->HardwareThreads >  32) dst.MaxTasks = 16384;
            if (host_cpu_info->HardwareThreads >  64) dst.MaxTasks = 32768;
            if (host_cpu_info->HardwareThreads > 128) dst.MaxTasks = 65536;
        }
        else
        {   // copy the value from the source configuration.
            dst.MaxTasks = src.MaxTasks;
        }
        // the maximum number of tasks per-tick should always be a power of two.
        if ((dst.MaxTasks & (dst.MaxTasks-1)) != 0)
        {   // round up to the next largest power-of-two.
            size_t n = 1;
            size_t m = dst.MaxTasks;
            while (n < m)
            {   // bump up to the next power-of-two.
                n <<= 1;
            }
            dst.MaxTasks = m;
        }
        // there is no per-thread memory requirement, so copy the source value directly.
        // calculate the amount of per-thread memory used in the general pool.
        dst.ArenaSize   = src.ArenaSize;
        compute_memory  = dst.MaxThreads * dst.ArenaSize;
        compute_memory += dst.MaxThreads * CalculateMemoryForTaskSource(2, dst.MaxTasks);
    }

    {   // validate configuration against system limits.
        if ((dst_config->PoolSize[TASK_POOL_GENERAL].MaxThreads  + 
             dst_config->PoolSize[TASK_POOL_COMPUTE].MaxThreads) > MAX_WORKER_THREADS)
        {   // there are too many worker threads for the software to support.
            ConsoleError("ERROR (%s): Too many worker threads for this scheduler implementation. Max is %u.\n", __FUNCTION__, MAX_WORKER_THREADS);
            performance_warnings = false;
            return -1;
        }
        if (dst_config->PoolSize[TASK_POOL_GENERAL].MaxTasks > MAX_TASKS_PER_BUFFER)
        {   // there are too many tasks for the scheduler to suport.
            ConsoleError("ERROR (%s): Too many tasks per-worker in the general pool. Max is %u.\n", __FUNCTION__, MAX_TASKS_PER_BUFFER);
            performance_warnings = false;
            return -1;
        }
        if (dst_config->PoolSize[TASK_POOL_COMPUTE].MaxTasks > MAX_TASKS_PER_BUFFER)
        {   // there are too many tasks for the scheduler to suport.
            ConsoleError("ERROR (%s): Too many tasks per-worker in the compute pool. Max is %u.\n", __FUNCTION__, MAX_TASKS_PER_BUFFER);
            performance_warnings = false;
            return -1;
        }
        if ((general_memory + compute_memory) >= memory.ullAvailPhys)
        {   // too much per-thread memory is requested; allocation will never succeed.
            ConsoleError("ERROR (%s): Too much thread-local memory requested. Requested %zu bytes, but only %zu bytes available.\n", __FUNCTION__, (general_memory+compute_memory), memory.ullAvailPhys);
            performance_warnings = false;
            return -1;
        }
        if (src_config->MainThreadArgs != NULL)
        {   // copy the value from the source configuration.
            dst_config->MainThreadArgs  = src_config->MainThreadArgs;
        }
        else
        {   // a valid WIN32_THREAD_ARGS must be supplied.
            ConsoleError("ERROR (%s): No WIN32_THREAD_ARGS specified in scheduler configuration.\n", __FUNCTION__);
            performance_warnings = false;
            return -1;
        }
    }

    {   // validate configuration performance against available resources.
        if ((double) (general_memory + compute_memory) >= (memory.ullAvailPhys * 0.8))
        {   // dangerously close to consuming all available physical memory in the system.
            ConsoleOutput("PERFORMANCE WARNING (%s): Scheduler will consume >= 80 percent of available physical memory. Swapping may occur.\n", __FUNCTION__);
            performance_warnings = true;
        }
        if (dst_config->PoolSize[TASK_POOL_COMPUTE].MaxThreads > host_cpu_info->HardwareThreads)
        {   // the host CPU is probably over-subscribed.
            ConsoleOutput("PERFORMANCE WARNING (%s): Compute pool has more threads than host CPU(s). Performance may be degraded.\n", __FUNCTION__);
            performance_warnings = true;
        }
    }

    size_t general_threads = dst_config->PoolSize[TASK_POOL_GENERAL].MaxThreads;
    size_t compute_threads = dst_config->PoolSize[TASK_POOL_COMPUTE].MaxThreads;
    size_t   total_threads = general_thread + compute_threads;
    if (src_config->MaxTaskSources < (total_threads + 1))
    {   // calculate an appropriate default value based on the thread pool sizes.
        dst_config->MaxTaskSources = (total_threads + 1); // the minimum acceptable value.
    }
    else
    {   // copy the value from the source configuration.
        dst_config->MaxTaskSources =  src_config->MaxTaskSources;
    }
    if (dst_config->MaxTaskSources > MAX_SCHEDULER_THREADS)
    {   // the global scheduler limit has been exceeded.
        ConsoleError("ERROR (%s): Too many task sources. Max is %u.\n", __FUNCTION__, MAX_SCHEDULER_THREADS);
        return -1;
    }
    return 0;
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
    if (buffer_size > MAX_TASKS_PER_BUFFER)
    {   // consider this to be an error; it's easily trapped during development.
        ConsoleError("ERROR (%s): Requested buffer size %zu exceeds maximum %u.\n", __FUNCTION__, buffer_size, MAX_TASKS_PER_BUFFER);
        return NULL;
    }
    if (scheduler->SourceCount == scheduler->MaxSources)
    {   // no additional sources can be allocated from the scheduler.
        ConsoleError("ERROR (%s): Max sources exceeded; increase limit WIN32_TASK_SCHEDULER_CONFIG::MaxTaskSources.\n", __FUNCTION__);
        return NULL;
    }
    size_t buffer_count = 2;
    size_t bytes_needed = CalculateMemoryForTaskSource(buffer_count, buffer_size);
    size_t    alignment = std::alignment_of<WIN32_TASK_SOURCE>::value;
    if (!ArenaCanAllocate(arena, bytes_needed, alignment))
    {   // the arena doesn't have sufficient memory to initialize a source with the requested attributes.
        ConsoleError("ERROR (%s): Insufficient memory in global arena; need %zu bytes.\n", __FUNCTION__, bytes_needed);
        return NULL;
    }

    WIN32_TASK_SOURCE *source =&scheduler->SourceList[scheduler->SourceCount];
    if (CreateComputeTaskQueue(&source->WorkQueue, buffer_size, arena) < 0)
    {   // unable to initialize the work-stealing dequeue for the source.
        ConsoleError("ERROR (%s): Failed to create compute task queue.\n", __FUNCTION__);
        return NULL;
    }
    if (CreateSemaphore(&source->StealSignal , 0, 1024) < 0)
    {   // unable to allocate the semaphore used for waking worker threads.
        ConsoleError("ERROR (%s): Failed to create work-stealing semaphore (%08X).\n", __FUNCTION__, GetLastError());
        return NULL;
    }
    source->GeneralWorkQueue =&scheduler->GeneralWorkQueue;
    source->SourceIndex      = scheduler->SourceCount;
    source->SourceCount      = scheduler->MaxSources;
    source->TaskSources      = scheduler->SourceList;
    source->TasksPerBuffer   = buffer_size;
    source->BufferIndex      = 0;
    source->ComputeTaskCount = 0;
    source->GeneralTaskCount = 0;
    for (size_t i = 0; i < buffer_count; ++i)
    {
        source->WorkItems [i] = PushArray<COMPUTE_TASK_DATA>(arena, buffer_size);
        source->WorkCounts[i] = PushArray<int32_t          >(arena, buffer_size);
        source->PermitList[i] = PushArray<PERMITS_LIST     >(arena, buffer_size);
    }
    scheduler->SourceCount++;
    return source;
}

/// @summary Wake exactly one or more waiting worker threads if there's work to be stolen. Call this if one or more items are added to a work queue.
/// @param source The WIN32_TASK_SOURCE owned by the calling thread.
/// @param count The number of resources made available in the work queue.
public_function void
SignalWaitingWorkers
(
    WIN32_TASK_SOURCE *source, 
    int                 count
)
{
    PostSemaphore(source->StealSignal, count);
}

/// @summary Create a new asynchronous task scheduler instance. The scheduler worker threads are launched separately.
/// @param scheduler The scheduler instance to initialize.
/// @param config The scheduler configuration.
/// @param arena The memory arena used to allocate scheduler memory. Per-worker memory is allocated directly from the OS.
/// @return A pointer to the new scheduler instance, or NULL.
public_function int
CreateScheduler
(
    WIN32_TASK_SCHEDULER              *scheduler, 
    WIN32_TASK_SCHEDULER_CONFIG const    *config,
    MEMORY_ARENA                          *arena
)
{   // validate the scheduler configuration.
    WIN32_TASK_SCHEDULER_CONFIG valid_config = {};
    bool                performance_warnings = false;
    if (CheckSchedulerConfiguration(&valid_config, config, performance_warnings) < 0)
    {   // no valid configuration can be obtained - some system limit was exceeded.
        ConsoleError("ERROR (%s): Invalid scheduler configuration.\n", __FUNCTION__);
        ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));
        return -1;
    }
    if (performance_warnings)
    {   // spit out an extra console message to try and get programmer attention.
        ConsoleOutput("PERFORMANCE WARNING (%s): Check console output above.\n", __FUNCTION__);
    }

    size_t mem_marker   = ArenaMarker(arena);
    size_t mem_required = CalculateMemoryForScheduler(&valid_config);
    if (!ArenaCanAllocate(arena, mem_required, std::alignment_of<WIN32_TASK_SCHEDULER>::value))
    {
        ConsoleError("ERROR (%s): Insufficient memory; %zu bytes required.\n", __FUNCTION__, mem_required);
        ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));
        return -1;
    }

    HANDLE ev_error        = NULL;
    HANDLE ev_launch       = NULL;
    HANDLE ev_terminate    = NULL;
    size_t general_threads = valid_config.PoolSize[TASK_POOL_GENERAL].MaxThreads;
    size_t general_tasks   = valid_config.PoolSize[TASK_POOL_GENERAL].MaxTasks;
    size_t compute_threads = valid_config.PoolSize[TASK_POOL_COMPUTE].MaxThreads;
    size_t compute_tasks   = valid_config.PoolSize[TASK_POOL_COMPUTE].MaxTasks;
    size_t max_max_tasks   = compute_tasks > general_tasks ? compute_tasks : general_tasks;
    WIN32_THREAD_POOL_CONFIG general_config = {};
    WIN32_THREAD_POOL_CONFIG compute_config = {};
    ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));

    // create scheduler worker thread synchronization objects.
    if ((ev_error = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // worker threads would have no way to signal a fatal error.
        ConsoleError("ERROR (%s): Failed to create worker error signal (%08X).\n", __FUNCTION__, GetLastError());
        goto cleanup_and_fail;
    }
    if ((ev_launch = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // worker threads would have no way to coordinate launching.
        ConsoleError("ERROR (%s): Failed to create worker launch signal (%08X).\n", __FUNCTION__, GetLastError());
        goto cleanup_and_fail;
    }
    if ((ev_terminate = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // worker threads would have no way to coordinate shutdown.
        ConsoleError("ERROR (%s): Failed to create worker shutdown signal (%08X).\n", __FUNCTION__, GetLastError());
        goto cleanup_and_fail;
    }

    // initialize the various coordination events and task source list.
    scheduler->MaxSources      = valid_config.MaxTaskSources;
    scheduler->SourceCount     = 0;
    scheduler->SourceList      = PushArray<WIN32_TASK_SOURCE>(valid_config.MaxTaskSources);
    scheduler->StartSignal     = ev_launch;
    scheduler->ErrorSignal     = ev_error;
    scheduler->TerminateSignal = ev_terminate;
    ZeroMemory(scheduler->SourceList, valid_config.MaxTaskSources * sizeof(WIN32_TASK_SOURCE));

    // task sources must be allocated for worker threads before the threads are spawned
    // (which happens when the thread pools are created, below.)
    // the assignment of the task sources is as follows:
    // [root][compute_workers][general_workers][user_threads]

    // allocate task source index 0 to the 'root' thread.
    if (NewTaskSource(scheduler, arena, max_max_tasks) == NULL)
    {   // the main thread would have no way to submit the root work tasks.
        ConsoleError("ERROR (%s): Failed to create the root task source.\n", __FUNCTION__);
        goto cleanup_and_fail;
    }

    // allocate task sources to the compute pool worker threads.
    for (size_t i = 0; i < compute_threads; ++i)
    {
        WIN32_TASK_SOURCE *worker_source = NewTaskSource(scheduler, arena, compute_tasks);
        if (worker_source == NULL)
        {   // the worker thread would have no way to submit compute tasks.
            ConsoleError("ERROR (%s): Failed to create the task source for compute worker %zu.\n", __FUNCTION__, i);
            goto cleanup_and_fail;
        }
    }

    // allocate task sources to the general pool worker threads.
    for (size_t i = 0; i < general_threads; ++i)
    {
        WIN32_TASK_SOURCE *worker_source = NewTaskSource(scheduler, arena, general_tasks);
        if (worker_source == NULL)
        {   // the worker thread would have no way to submit compute tasks.
            ConsoleError("ERROR (%s): Failed to create the task source for general worker %zu.\n", __FUNCTION__, i);
            goto cleanup_and_fail;
        }
    }

    // initialize, but do not launch, the general task thread pool.
    general_config.TaskScheduler     = scheduler;
    general_config.ThreadMain        = GeneralWorkerMain;
    general_config.MainThreadArgs    = valid_config.MainThreadArgs;
    general_config.MinThreads        = valid_config.PoolSize[TASK_POOL_GENERAL].MinThreads;
    general_config.MaxThreads        = valid_config.PoolSize[TASK_POOL_GENERAL].MaxThreads;
    general_config.WorkerArenaSize   = valid_config.PoolSize[TASK_POOL_GENERAL].ArenaSize;
    general_config.WorkerSourceIndex = compute_threads + 1; // see above
    general_config.StartSignal       = ev_launch;
    general_config.ErrorSignal       = ev_error;
    general_config.TerminateSignal   = ev_terminate;
    general_config.WorkerFlags       = WORKER_FLAGS_GENERAL;
    if (CreateThreadPool(&scheduler->GeneralPool, &general_config, arena) < 0)
    {   // the scheduler would have no pool in which to execute general tasks.
        ConsoleError("ERROR (%s): Failed to create the general thread pool.\n", __FUNCTION__);
        goto cleanup_and_fail;
    }

    // initialize, but do not launch, the compute task thread pool.
    compute_config.TaskScheduler     = scheduler;
    compute_config.ThreadMain        = ComputeWorkerMain;
    compute_config.MainThreadArgs    = valid_config.MainThreadArgs;
    compute_config.MinThreads        = valid_config.PoolSize[TASK_POOL_COMPUTE].MinThreads;
    compute_config.MaxThreads        = valid_config.PoolSize[TASK_POOL_COMPUTE].MaxThreads;
    compute_config.WorkerArenaSize   = valid_config.PoolSize[TASK_POOL_COMPUTE].ArenaSize;
    compute_config.WorkerSourceIndex = 1; // see above
    compute_config.StartSignal       = ev_launch;
    compute_config.ErrorSignal       = ev_error;
    compute_config.TerminateSignal   = ev_terminate;
    compute_config.WorkerFlags       = WORKER_FLAGS_COMPUTE;
    if (CreateThreadPool(&scheduler->ComputePool, &compute_config, arena) < 0)
    {   // the scheduler would have no pool in which to execute compute tasks.
        ConsoleError("ERROR (%s): Failed to create the compute thread pool.\n", __FUNCTION__);
        goto cleanup_and_fail;
    }

    // create the work queue and associated synchronization objects for general pool tasks.
    if (CreateSemaphore(&scheduler->GeneralWorkSignal, 0, 1024) < 0)
    {   // general pool worker threads would have no way to know that work is waiting.
        ConsoleError("ERROR (%s): Failed to create the general work queue semaphore.\n", __FUNCTION__);
        goto cleanup_and_fail;
    }
    if (CreateGeneralTaskQueue(&scheduler->GeneralWorkQueue, general_tasks, arena) < 0)
    {   // general pool worker threads would have no way to get work to execute.
        ConsoleError("ERROR (%s): Failed to create the general work queue.\n", __FUNCTION__);
        goto cleanup_and_fail;
    }

    return 0;

cleanup_and_fail:
    TerminateThreadPool(&scheduler->ComputePool);
    TerminateThreadPool(&scheduler->GeneralPool);
    for (size_t i = 0, n = scheduler->SourceCount; i < n; ++i)
    {   // clean up the semaphore for any allocated task sources.
        DeleteSemaphore(&scheduler->SourceList[i].StartSignal);
    }
    if (ev_terminate != NULL) CloseHandle(ev_terminate);
    if (ev_launch != NULL) CloseHandle(ev_launch);
    if (ev_error != NULL) CloseHandle(ev_error);
    DeleteSemaphore(&scheduler->GeneralWorkSignal);
    ArenaResetToMarker(arena, mem_marker);
    ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));
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
    if (scheduler->StartSignal != NULL)
    {   // start all of the worker threads looking for work.
        SetEvent(scheduler->StartSignal);
    }
}

/// @summary Notify all task scheduler worker threads to shutdown, and clean up scheduler resources. Ensure that no more tasks will be created prior to calling this function.
/// @param scheduler The task scheduler to halt.
public_function void
HaltScheduler
(
    WIN32_TASK_SCHEDULER *scheduler
)
{   // signal all worker threads to exit, and wait for them.
    TerminateThreadPool(&scheduler->ComputePool);
    TerminateThreadPool(&scheduler->GeneralPool);
    // clean up the resources allocated to any task sources.
    for (size_t i = 0, n = scheduler->SourceCount; i < n; ++i)
    {   // clean up the semaphore allocated to the task source.
        DeleteSemaphore(&scheduler->SourceList[i].StartSignal);
    }
    if (scheduler->TerminateSignal != NULL) CloseHandle(scheduler->TerminateSignal);
    if (scheduler->StartSignal     != NULL) CloseHandle(scheduler->StartSignal);
    if (scheduler->ErrorSignal     != NULL) CloseHandle(scheduler->ErrorSignal);
    DeleteSemaphore(&scheduler->GeneralWorkSignal);
    ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));
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

/// @summary Spawn a new task to execute on the general pool.
/// @param source The WIN32_TASK_SOURCE owned by the calling thread.
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @return The identifier of the new task, or INVALID_TASK_ID.
public_function task_id_t
NewGeneralTask
(
    WIN32_TASK_SOURCE         *source, 
    GENERAL_TASK_ENTRYPOINT task_main, 
    void   const           *task_args, 
    size_t const            args_size
)
{
    if (args_size > GENERAL_TASK_DATA::MAX_DATA)
    {   // only create the task if the supplied data will fit.
        ConsoleError("ERROR (%s): Task argument data exceeds maximum allowable (%zu bytes, max %zu bytes).\n", __FUNCTION__, args_size, GENERAL_TASK_DATA::MAX_DATA);
        return INVALID_TASK_ID;
    }
    WIN32_TASK_SCHEDULER *scheduler = source->TaskScheduler;
    WIN32_THREAD_POOL  *thread_pool =&scheduler->GeneralPool;
    uint32_t             task_index = source->GeneralTaskCount;
    task_id_t  task_id = MakeTaskId(0, TASK_POOL_GENERAL, task_index, source->SourceIndex);
    if (GeneralTaskQueuePut(source->GeneralWorkQueue, task_id, task_main, task_args, task_size) == 0)
    {   // wake up a waiting worker thread and return the task ID.
        PostSemaphore(&scheduler->GeneralWorkSignal, 1);
        source->GeneralTaskCount++;
        return task_id;
    }
    else
    {   // the work queue is full - fail to create the task.
        ConsoleError("ERROR (%s): General work queue is full.\n", __FUNCTION__);
        return INVALID_TASK_ID;
    }
}

/// @summary Spawn a new task to execute on the compute pool. After spawning any child tasks, call FinishComputeTask to complete the task definition.
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
/// @param parent_id The task ID of the parent task, as returned by the New*Task function used to create the parent task.
/// @param wait_task The identifier of the compute task that must complete prior to executing this new task, or INVALID_TASK_ID.
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
    if (wait_task != INVALID_TASK_ID && IsGeneralTask(wait_task))
    {   // general tasks do not support dependencies, and compute tasks cannot wait on general tasks.
        ConsoleError("ERROR (%s): Compute tasks cannot wait on general tasks (id = %08X).\n", __FUNCTION__, wait_task);
        return INVALID_TASK_ID;
    }
    if (IsComputeTask(parent_id))
    {   // retrieve the information we need about the parent task.
        parent_task  = GetTaskWorkItem (parent_id, source->TaskSources);
        parent_count = GetTaskWorkCount(parent_id, source->TaskSources);
        // increment the outstanding work counter on the parent.
        InterlockedIncrement((volatile LONG*) parent_count);
        // define the child task. the parent work counter will be decremented when the child finishes executing.
        return DefineComputeTask(source, task_main, task_args, args_size, parent_id, wait_task);
    }
    else if (IsGeneralTask(parent_id))
    {   // this is supported for convenience and identification purposes, but is otherwise meaningless.
        return DefineComputeTask(source, task_main, task_args, args_size, parent_id, wait_task);
    }
    else
    {   // the parent ID isn't valid, so fail the child creation.
        return INVALID_TASK_ID;
    }
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
                SignalWaitingWorkers(source, n);
            }
            // decrement the work counter on any parent task.
            return n + FinishComputeTask(source, work_item->ParentTask);
        }
    }
    return 0;
}

