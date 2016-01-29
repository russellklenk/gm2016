/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement two task schedulers. One implementation is designed for
/// non-blocking, compute-oriented tasks. The other implementation is designed 
/// for more traditional asynchronous tasks such as database calls.
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Define the mask and shift values for the constituient parts of a task ID.
/// The task ID layout is as follows:
/// 31.............................0
/// VWWWWWWWWWWWWIIIIIIIIIIIIIIIICBB
/// 
/// Where the letters mean the following:
/// V: Set if the ID is valid, clear if the ID is not valid.
/// W: The zero-based index of the worker thread. The system supports up to 4095 worker threads, plus 1 to represent the thread that submits the root tasks.
/// I: The zero-based index of the task data within the worker thread's tick buffer.
/// C: Set if the task is a compute-oriented task, clear if the task is a more traditional async task.
/// B: The zero-based index of the task buffer. The system supports up to 4 ticks in-flight simultaneously.
#ifndef TASK_ID_LAYOUT_DEFINED
#define TASK_ID_LAYOUT_DEFINED
#define TASK_ID_MASK_TICK_P      (0x00000003UL)
#define TASK_ID_MASK_TICK_U      (0x00000003UL)
#define TASK_ID_MASK_TYPE_P      (0x00000004UL)
#define TASK_ID_MASK_TYPE_U      (0x00000001UL)
#define TASK_ID_MASK_INDEX_P     (0x0007FFF8UL)
#define TASK_ID_MASK_INDEX_U     (0x0000FFFFUL)
#define TASK_ID_MASK_THREAD_P    (0x7FF80000UL)
#define TASK_ID_MASK_THREAD_U    (0x00000FFFUL)
#define TASK_ID_MASK_VALID_P     (0x80000000UL)
#define TASK_ID_MASK_VALID_U     (0x00000001UL)
#define TASK_ID_SHIFT_TICK       (0)
#define TASK_ID_SHIFT_TYPE       (2)
#define TASK_ID_SHIFT_INDEX      (3)
#define TASK_ID_SHIFT_THREAD     (19)
#define TASK_ID_SHIFT_VALID      (31)
#endif

/// @summary Define the maximum number of worker threads supported by the scheduler.
#ifndef MAX_WORKER_THREADS
#define MAX_WORKER_THREADS       (4095)
#endif

/// @summary Define the maximum number of threads that can submit jobs to the scheduler. This is the number of worker threads plus one, to account for the main thread that submits the root tasks.
#ifndef MAX_SCHEDULER_THREADS
#define MAX_SCHEDULER_THREADS    (4096)
#endif

/// @summary Define the maximum number of simultaneous ticks in-flight. The main thread will block and stop submitting ticks when this limit is reached. The runtime limit may be lower.
#ifndef MAX_TICKS_IN_FLIGHT
#define MAX_TICKS_IN_FLIGHT      (4)
#endif

/// @summary Define the maximum number of tasks that can be submitted during any given tick. The runtime limit may be lower.
#ifndef MAX_TASKS_PER_TICK
#define MAX_TASKS_PER_TICK       (65536)
#endif

/// @summary Define the identifier returned to represent an invalid task ID.
#ifndef INVALID_TASK_ID
#define INVALID_TASK_ID          ((task_id_t) 0x7FFFFFFFUL)
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Use a unique 32-bit integer to identify a task.
typedef uint32_t task_id_t;

/// @summary Define the function signature for a task entrypoint.
typedef int (*TASK_ENTRY)(struct TASK_ARGS*);

/// @summary Define identifiers for task ID validity. An ID can only be valid or invalid.
enum TASK_ID_TYPE : uint32_t
{
    TASK_ID_TYPE_INVALID    = 0,    /// The task identifier specifies an invalid task.
    TASK_ID_TYPE_VALID      = 1,    /// The task identifier specifies a valid task.
};

/// @summary Define identifiers for supported scheduler types. Only two scheduler types are supported since only one bit is available in the task ID.
enum SCHEDULER_TYPE : uint32_t
{
    SCHEDULER_TYPE_ASYNC    = 0,    /// Identifies a standard asynchronous task scheduler.
    SCHEDULER_TYPE_COMPUTE  = 1,    /// Identifies a compute-oriented task scheduler.
};

/// @summary Define a structure specifying the constituent parts of a task ID.
struct TASK_ID_PARTS
{
    uint32_t      ValidTask;        /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t      SchedulerType;    /// One of SCHEDULER_TYPE specifying the scheduler that owns the task.
    uint32_t      ThreadIndex;      /// The zero-based index of the thread that defines the task.
    uint32_t      BufferIndex;      /// The zero-based index of the thread-local buffer that defines the task.
    uint32_t      TaskIndex;        /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define a structure used to specify data used to configure a task scheduler instance at creation time.
struct TASK_SCHEDULER_CONFIG
{
    size_t        MaxActiveTicks;   /// The maximum number of application ticks in-flight at any given time.
    size_t        MaxWorkerThreads; /// The maximum number of worker threads that can be spawned.
    size_t        MaxTasksPerTick;  /// The maximum number of tasks that can be created during a single application tick.
    size_t        MaxTaskArenaSize; /// The number of bytes to allocate for each thread-local arena.
};

/// @summary Define the data associated with a single work item.
struct WORK_ITEM
{
    TASK_ENTRY    TaskMain;         /// The task entry point.
    int32_t       WorkCount;        /// The number of outstanding work items associated with the task.
    task_id_t     Dependency;       /// THe task that must complete before this task is launched.
    char          ArgsData[48];     /// Space for additional data to be passed to the task.
};

/// @summary Define the data associated with task definitions for a single in-flight tick.
struct TASK_BUFFER
{
    size_t        TaskCount;        /// The number of tasks allocated from the buffer.
    WORK_ITEM    *WorkItems;        /// Fixed-length storage for storing data associated with each task.
    // TODO(rlk): Dependency lists - what tasks depend on this task?
};

/// @summary Define the data associated with a single worker thread.
struct TASK_WORKER
{
    size_t        MaxTasksPerTick;  /// The maximum number of tasks that can be created in a given tick.
    MEMORY_ARENA  ThreadArena;      /// The thread-local memory arena.
    TASK_BUFFER   TaskList[4];      /// The per-tick data used to track tasks created on that tick.
    // TODO(rlk): WorkQueue
};

/// @summary Define the data associated with an asynchronous task scheduler.
struct WIN32_ASYNC_TASK_SCHEDULER
{
    HANDLE        ErrorSignal;      /// 
    HANDLE        LaunchSignal;     /// 

    size_t        ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE       *OSThreadHandle;   /// The operating system thread handle for each worker thread.
    TASK_WORKER  *WorkerThreads;    /// 
};

/// @summary Define the data associated with a compute-oriented task scheduler.
struct WIN32_COMPUTE_TASK_SCHEDULER
{
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Create a task ID from its constituient parts.
/// @param tick_index The zero-based index of the in-flight tick.
/// @param scheduler_type The type of scheduler that owns the task. One of SCHEDULER_TYPE.
/// @param task_index The zero-based index of the task within the task list for this tick in the thread that created the task.
/// @param thread_index The zero-based index of the thread that created the task.
/// @param task_id_type Indicates whether the task ID is valid. One of TASK_ID_TYPE.
/// @return The task identifier.
public_function inline task_id_t
MakeTaskId
(
    uint32_t     tick_index, 
    uint32_t scheduler_type, 
    uint32_t     task_index, 
    uint32_t   thread_index, 
    uint32_t   task_id_type = TASK_ID_TYPE_VALID
)
{
    return ((tick_index     & TASK_ID_MASK_TICK_U  ) << TASK_ID_SHIFT_TICK  ) | 
           ((scheduler_type & TASK_ID_MASK_TYPE_U  ) << TASK_ID_SHIFT_TYPE  ) | 
           ((task_index     & TASK_ID_MASK_INDEX_U ) << TASK_ID_SHIFT_INDEX ) |
           ((thread_index   & TASK_ID_MASK_THREAD_U) << TASK_ID_SHIFT_THREAD) | 
           ((task_id_type   & TASK_ID_MASK_VALID_U ) << TASK_ID_SHIFT_VALID );
}

/// @summary Determine whether an ID identifies a valid task.
/// @param id The task identifier to check.
/// @return true if the identifier specifies a valid task.
public_function inline bool
IsValidTask
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0);
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
    parts->BufferIndex   = (id & TASK_ID_MASK_TICK_P  ) >> TASK_ID_SHIFT_TICK;
    parts->TaskIndex     = (id & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
}

