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
/// I: The zero-based index of the task data within the worker thread's task buffer.
/// C: Set if the task is a compute-oriented task, clear if the task is a more traditional async task.
/// B: The zero-based index of the task buffer. The system supports up to 4 ticks in-flight simultaneously.
#ifndef TASK_ID_LAYOUT_DEFINED
#define TASK_ID_LAYOUT_DEFINED
#define TASK_ID_MASK_BUFFER_P             (0x00000003UL)
#define TASK_ID_MASK_BUFFER_U             (0x00000003UL)
#define TASK_ID_MASK_TYPE_P               (0x00000004UL)
#define TASK_ID_MASK_TYPE_U               (0x00000001UL)
#define TASK_ID_MASK_INDEX_P              (0x0007FFF8UL)
#define TASK_ID_MASK_INDEX_U              (0x0000FFFFUL)
#define TASK_ID_MASK_THREAD_P             (0x7FF80000UL)
#define TASK_ID_MASK_THREAD_U             (0x00000FFFUL)
#define TASK_ID_MASK_VALID_P              (0x80000000UL)
#define TASK_ID_MASK_VALID_U              (0x00000001UL)
#define TASK_ID_SHIFT_BUFFER              (0)
#define TASK_ID_SHIFT_TYPE                (2)
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

/// @summary Define the maximum number of simultaneous ticks in-flight. The main thread will block and stop submitting ticks when this limit is reached. The runtime limit may be lower.
#ifndef MAX_TICKS_IN_FLIGHT
#define MAX_TICKS_IN_FLIGHT               (4)
#endif

/// @summary Define the maximum number of tasks that can be submitted during any given tick. The runtime limit may be lower.
#ifndef MAX_TASKS_PER_TICK
#define MAX_TASKS_PER_TICK                (65536)
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

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Use a unique 32-bit integer to identify a task.
typedef uint32_t task_id_t;

/// @summary Define the function signature for a task entrypoint.
typedef int (*TASK_ENTRY)(task_id_t, struct TASK_SOURCE*, struct WORK_ITEM*, MEMORY_ARENA*, WIN32_THREAD_ARGS*);

/// @summary Define identifiers for task ID validity. An ID can only be valid or invalid.
enum TASK_ID_TYPE : uint32_t
{
    TASK_ID_TYPE_INVALID    = 0,     /// The task identifier specifies an invalid task.
    TASK_ID_TYPE_VALID      = 1,     /// The task identifier specifies a valid task.
};

/// @summary Define identifiers for supported scheduler types. Only two scheduler types are supported since only one bit is available in the task ID.
enum SCHEDULER_TYPE : uint32_t
{
    SCHEDULER_TYPE_ASYNC    = 0,     /// Identifies a standard asynchronous task scheduler.
    SCHEDULER_TYPE_COMPUTE  = 1,     /// Identifies a compute-oriented task scheduler.
};

/// @summary Define a structure specifying the constituent parts of a task ID.
struct TASK_ID_PARTS
{
    uint32_t            ValidTask;        /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t            SchedulerType;    /// One of SCHEDULER_TYPE specifying the scheduler that owns the task.
    uint32_t            ThreadIndex;      /// The zero-based index of the thread that defines the task.
    uint32_t            BufferIndex;      /// The zero-based index of the thread-local buffer that defines the task.
    uint32_t            TaskIndex;        /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define a structure used to specify data used to configure a task scheduler instance at creation time.
struct TASK_SCHEDULER_CONFIG
{
    size_t              MaxActiveTicks;   /// The maximum number of application ticks in-flight at any given time.
    size_t              MaxWorkerThreads; /// The maximum number of worker threads that can be spawned.
    size_t              MaxTasksPerTick;  /// The maximum number of tasks that can be created during a single application tick.
    size_t              MaxTaskArenaSize; /// The number of bytes to allocate for each thread-local arena.
};

/// @sumary Define the data associated with an async scheduler worker thread.
struct ASYNC_WORKER
{
    WIN32_THREAD_ARGS  *MainThreadArgs;   /// Global data managed by the main thread.
    HANDLE              ReadySignal;      /// Signal set by the worker to indicate that its initialization is complete and it is ready to run.
    HANDLE              StartSignal;      /// Signal set by the scheduler to allow the worker thread to begin polling for work.
    HANDLE              ErrorSignal;      /// Signal set by any worker to indicate that a fatal error has occurred and the worker should die.
    HANDLE              HaltSignal;       /// Signal set by any thread to stop all worker threads.
    uint32_t            WorkerIndex;      /// The zero-based index of the worker thread within the scheduler.
};

/// @summary Define the data associated with a work item.
struct WORK_ITEM
{   static size_t const MAX_DATA = 48;    /// The maximum amount of data that can be passed to the task.
    uint32_t            Reserved0;        /// This value is reserved for future use.
    task_id_t           ParentTask;       /// The identifier of the parent task, or INVALID_TASK_ID.
    TASK_ENTRY          TaskMain;         /// The task entry point.
#if TARGET_ARCHITECTURE == ARCHITECTURE_X86_32 || TARGET_ARCHITECTURE == ARCHITECTURE_ARM_32
    uint32_t            Reserved1;        /// Padding; unused.
#endif
    uint8_t             TaskArgs[MAX_DATA]; /// User-supplied argument data associated with the work item.
};

/// @summary Defines the data associated with a set of tasks waiting on another task to complete.
struct PERMITS_LIST
{   static size_t const ALIGNMENT = CACHELINE_SIZE;
    static size_t const MAX_TASKS = 15;   /// The maximum number of task IDs that can be stored in a permits list.
    int32_t             Count;            /// The number of items in the permits list.
    task_id_t           Tasks[MAX_TASKS]; /// The task IDs in the permits list. This is the set of tasks to launch when the owning task completes.
};

/// @summary Define the data representing a work-stealing deque of task identifiers. Each compute worker thread maintains its own queue.
/// The worker thread can perform push and take operations. Other worker threads can perform concurrent steal operations.
struct TASK_QUEUE
{   static size_t const ALIGNMENT           = CACHELINE_SIZE;
    static size_t const PAD                 = CACHELINE_SIZE - sizeof(std::atomic<int64_t>);
    typedef std::atomic<int64_t> index_t; /// An atomic index used to represent the ends of the queue.
    index_t             Pub;              /// The public end of the deque, updated by steal operations (Top).
    uint8_t             Pad0[PAD];        /// Padding separating the public data from the private data.
    index_t             Prv;              /// The private end of the deque, updated by push and take operations (Bottom).
    uint8_t             Pad1[PAD];        /// Padding separating the private data from the storage data.
    int64_t             Mask;             /// The bitmask used to map the Top and Bottom indices into the storage array.
    task_id_t          *Tasks;            /// The identifiers for the tasks in the queue.
};

/// @summary Define the data associated with a thread that can produce tasks (but not necessarily execute them.)
/// Each worker thread in the scheduler thread pool is a TASK_SOURCE that can also execute tasks. The function 
/// The maximum number of task sources is fixed at scheduler creation time.
struct TASK_SOURCE
{   static size_t const ALIGNMENT           = CACHELINE_SIZE;

#pragma warning(push)
#pragma warning(disable:4324)             /// WARNING: structure was padded due to __declspec(align())
    cacheline_align
    TASK_QUEUE          WorkQueue;        /// The queue of ready-to-run task IDs.
#pragma warning(pop)

    HANDLE              StealSignal;      /// An auto-reset event used to wake a single thread when work can be stolen.
    size_t              GroupIndex;       /// The zero-based index of the source group.
    uint32_t            SourceGroupSize;  /// The number of sources in the source group. Each source group may contain up to 64 sources. Constant.
    uint32_t            SourceIndex;      /// The zero-based index of the source within the source list. Constant.

    size_t              MaxTicksInFlight; /// The maximum number of ticks in-flight at any given time. Constant.
    size_t              MaxTasksPerTick;  /// The maximum number of tasks that can be created in a given tick. Constant.
    size_t              TaskSourceCount;  /// The total number of task sources defined in the scheduler. Constant.
    TASK_SOURCE        *TaskSources;      /// The list of per-source state for each task source. Managed by the scheduler.
    
    uint32_t            BufferIndex;      /// The zero-based index of the task buffer being written to.
    uint32_t            TaskCount;        /// The zero-based index of the next available task in the current buffer.

    WORK_ITEM          *WorkItems [4];    /// The work item definitions for each task, for each in-flight tick.
    int32_t            *WorkCounts[4];    /// The outstanding work counter for each task, for each in-flight tick.
    PERMITS_LIST       *PermitList[4];    /// The permits list for each task, for each in-flight tick.
};

/// @summary Define the data associated with a compute scheduler worker thread.
struct COMPUTE_WORKER
{
    WIN32_THREAD_ARGS  *MainThreadArgs;   /// Global data managed by the main thread.
    MEMORY_ARENA        ThreadArena;      /// The user-facing thread-local memory arena.
    HANDLE              ReadySignal;      /// Signal set by the worker to indicate that its initialization is complete and it is ready to run.
    HANDLE              StartSignal;      /// Signal set by the scheduler to allow the worker thread to begin polling for work.
    HANDLE              ErrorSignal;      /// Signal set by any worker to indicate that a fatal error has occurred and the worker should die.
    HANDLE              HaltSignal;       /// Signal set by any thread to stop all worker threads.
    size_t              ThreadIndex;      /// The zero-based index of the thread in the worker thread pool.
    TASK_SOURCE        *ThreadSource;     /// The TASK_SOURCE allocated for this worker thread.
    WIN32_MEMORY_ARENA  OSThreadArena;    /// The underlying OS thread-local memory arena.
};

/// @summary Define the data associated with an asynchronous task scheduler.
struct WIN32_ASYNC_TASK_SCHEDULER
{
    HANDLE              ErrorSignal;      /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE              StartSignal;      /// Manual-reset event signaled when worker threads should start running tasks.
    HANDLE              HaltSignal;       /// Manual-reset event signaled when the scheduler is being shut down.

    size_t              MaxThreads;       /// The maximum number of worker threads the scheduler can spawn.
    size_t              ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int       *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE             *OSThreadHandle;   /// The operating system thread handle for each worker thread.
    ASYNC_WORKER       *WorkerState;      /// The state data for each worker thread.
};

/// @summary Define the data associated with a compute-oriented task scheduler.
struct WIN32_COMPUTE_TASK_SCHEDULER
{
    HANDLE              ErrorSignal;      /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE              StartSignal;      /// Manual-reset event signaled when worker threads should start running tasks.
    HANDLE              HaltSignal;       /// Manual-reset event signaled when the scheduler is being shut down.

    size_t              ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int       *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE             *OSThreadHandle;   /// The operating system thread handle for each worker thread.
    COMPUTE_WORKER     *WorkerState;      /// The state data for each worker thread.

    size_t              MaxTicksInFlight; /// The maximum number of ticks in-flight at any given time. Constant.
    size_t              MaxTasksPerTick;  /// The maximum number of tasks that can be created per-tick. Constant.
    size_t              MaxSourceCount;   /// The maximum number of allowable task sources. Always at least ThreadCount+1.
    size_t              SourceCount;      /// The number of task sources currently defined.
    TASK_SOURCE        *SourceList;       /// The list registered task sources, of which SourceCount are valid.
};

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/
public_function TASK_SOURCE* NewTaskSource(WIN32_COMPUTE_TASK_SCHEDULER*, MEMORY_ARENA*, size_t, size_t);
public_function int32_t      FinishTask(TASK_SOURCE*, task_id_t);

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Create a task ID from its constituient parts.
/// @param buffer_index The zero-based index of the task buffer.
/// @param scheduler_type The type of scheduler that owns the task. One of SCHEDULER_TYPE.
/// @param task_index The zero-based index of the task within the task list for this tick in the thread that created the task.
/// @param thread_index The zero-based index of the thread that created the task.
/// @param task_id_type Indicates whether the task ID is valid. One of TASK_ID_TYPE.
/// @return The task identifier.
internal_function inline task_id_t
MakeTaskId
(
    uint32_t   buffer_index, 
    uint32_t scheduler_type, 
    uint32_t     task_index, 
    uint32_t   thread_index, 
    uint32_t   task_id_type = TASK_ID_TYPE_VALID
)
{
    return ((buffer_index   & TASK_ID_MASK_BUFFER_U) << TASK_ID_SHIFT_BUFFER) | 
           ((scheduler_type & TASK_ID_MASK_TYPE_U  ) << TASK_ID_SHIFT_TYPE  ) | 
           ((task_index     & TASK_ID_MASK_INDEX_U ) << TASK_ID_SHIFT_INDEX ) |
           ((thread_index   & TASK_ID_MASK_THREAD_U) << TASK_ID_SHIFT_THREAD) | 
           ((task_id_type   & TASK_ID_MASK_VALID_U ) << TASK_ID_SHIFT_VALID );
}

/// @summary Determine whether an ID identifies a valid task.
/// @param id The task identifier to check.
/// @return true if the identifier specifies a valid task.
internal_function inline bool
IsValidTask
(
    task_id_t id
)
{
    return (((id & TASK_ID_MASK_VALID_P) >> TASK_ID_SHIFT_VALID) != 0);
}

/// @summary Retrieve the work item for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_COMPUTE_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the work item data.
internal_function inline WORK_ITEM*
GetTaskWorkItem
(
    task_id_t           task,
    TASK_SOURCE *source_list
)
{   // NOTE: this function does not check the validity of the task ID.
    uint32_t const thread_index = (task & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    uint32_t const buffer_index = (task & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[thread_index].WorkItems[buffer_index][task_index];
}

/// @summary Retrieve the work item for a given task ID.
/// @param parts The parsed task identifier. This function does not check TASK_ID_PARTS::ValidTask.
/// @param source_list The list of state data associated with each task source; either WIN32_COMPUTE_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the work item data.
internal_function inline WORK_ITEM*
GetTaskWorkItem
(
    TASK_ID_PARTS const      &parts,
    TASK_SOURCE        *source_list
)
{
    return  &source_list[parts.ThreadIndex].WorkItems[parts.BufferIndex][parts.TaskIndex];
}

/// @summary Retrieve the work counter for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_COMPUTE_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the work counter associated with the task.
internal_function inline int32_t*
GetTaskWorkCount
(
    task_id_t           task,
    TASK_SOURCE *source_list
)
{   // NOTE: this function does not check the validity of the task ID.
    uint32_t const thread_index = (task & TASK_ID_MASK_THREAD_P) >> TASK_ID_SHIFT_THREAD;
    uint32_t const buffer_index = (task & TASK_ID_MASK_BUFFER_P) >> TASK_ID_SHIFT_BUFFER;
    uint32_t const   task_index = (task & TASK_ID_MASK_INDEX_P ) >> TASK_ID_SHIFT_INDEX;
    return &source_list[thread_index].WorkCounts[buffer_index][task_index];
}

/// @summary Retrieve the list of permits for a given task ID.
/// @param task The task identifier. This function does not validate the task ID.
/// @param source_list The list of state data associated with each task source; either WIN32_COMPUTE_TASK_SCHEDULER::SourceList or COMPUTE_WORKER::ThreadSource->TaskSources.
/// @return A pointer to the permits list associated with the task.
internal_function inline PERMITS_LIST*
GetTaskPermitsList
(
    task_id_t           task, 
    TASK_SOURCE *source_list
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
TaskQueuePush
(
    TASK_QUEUE *queue, 
    task_id_t    task
)
{   // Pub = Top (Steal), Prv = Bottom (Push/Take)
    int64_t  b   = queue->Prv.load(std::memory_order_relaxed);
    // enqueue the new task at the private end of the queue.
    queue->Tasks[b & queue->Mask] = task;
    // ensure that the task ID is written to the Tasks array.
    std::atomic_thread_fence(std::memory_order_release);
    // make the new task visible to other threads.
    queue->Prv.store(b + 1, std::memory_order_relaxed);
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
    int64_t  b = queue->Prv.load(std::memory_order_relaxed) - 1;
    queue->Prv.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    int64_t  t = queue->Pub.load(std::memory_order_relaxed);
    if (t <= b)
    {   // the task queue is non-empty.
        task_id_t task = queue->Tasks[b & queue->Mask];

        if (t != b)
        {   // there's at least one more item in the queue; no need to race.
            return task;
        }
        // this was the last item in the queue. race to claim it.
        if (!queue->Pub.compare_exchange_strong(t, t+1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // this thread lost the race.
            task = INVALID_TASK_ID;
        }
        queue->Prv.store(t + 1, std::memory_order_relaxed); // WAS: b + 1
        return task;
    }
    else
    {
        queue->Prv.store(t, std::memory_order_relaxed);
        return INVALID_TASK_ID; // the queue is currently empty.
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
    int64_t t = queue->Pub.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    int64_t b = queue->Prv.load(std::memory_order_acquire);

    if (t < b)
    {   // the task queue is non-empty. save the task ID.
        task_id_t task = queue->Tasks[t & queue->Mask];
        // race with other threads to claim the item.
        if (queue->Pub.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
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
TaskQueueClear
(
    TASK_QUEUE *queue
)
{
    queue->Pub.store(0, std::memory_order_relaxed);
    queue->Prv.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

/// @summary Allocate the memory for a task queue and initialize the queue to empty.
/// @param queue The task queue to initialize.
/// @param capacity The capacity of the queue. This value must be a power of two greater than zero.
/// @param arena The memory arena to allocate from. The caller should ensure that sufficient memory is available.
internal_function void
CreateTaskQueue
(
    TASK_QUEUE   *queue, 
    size_t     capacity, 
    MEMORY_ARENA *arena
)
{   // the capacity must be a power of two.
    assert((capacity & (capacity - 1)) == 0);
    queue->Pub.store(0, std::memory_order_relaxed);
    queue->Prv.store(0, std::memory_order_relaxed);
    queue->Mask  = int64_t(capacity) - 1;
    queue->Tasks = PushArray<task_id_t>(arena, capacity);
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

/// @summary Calculate the amount of memory required to store task source data.
/// @param max_active_ticks The maximum number of ticks in-flight at any given time. The maximum value is 4.
/// @param max_tasks_per_tick The maximum number of tasks that the source can create during any tick. The maximum value is 65536.
/// @return The minimum number of bytes required to store the task data, not including the size of the TASK_SOURCE instance.
internal_function size_t
CalculateMemoryForTaskSource
(
    size_t   max_active_ticks, 
    size_t max_tasks_per_tick 
)
{
    size_t bytes_needed = 0;
    bytes_needed += CalculateMemoryForTaskQueue(max_tasks_per_tick);                             // WorkQueue
    bytes_needed += AllocationSizeForArray<WORK_ITEM   >(max_tasks_per_tick) * max_active_ticks; // WorkItems
    bytes_needed += AllocationSizeForArray<int32_t     >(max_tasks_per_tick) * max_active_ticks; // WorkCounts
    bytes_needed += AllocationSizeForArray<PERMITS_LIST>(max_tasks_per_tick) * max_active_ticks; // PermitList
    return bytes_needed;
}

/// @summary Execute a compute task on the calling thread.
/// @param task The identifier of the task to execute.
/// @param worker_source The TASK_SOURCE owned by the calling thread.
/// @param task_arena The memory arena to use for task-local memory allocations. The arena is reset prior to task execution.
/// @param thread_args Global data and state managed by the main thread.
/// @return The task exit code.
internal_function int
ExecuteComputeTask
(
    task_id_t                 task,
    TASK_SOURCE     *worker_source, 
    MEMORY_ARENA       *task_arena, 
    WIN32_THREAD_ARGS *thread_args
)
{   
    int        rcode = 0;
    WORK_ITEM *work_item;
    if (IsValidTask(task) && (work_item = GetTaskWorkItem(task, worker_source->TaskSources)) != NULL)
    {
        ArenaReset(task_arena);
        rcode = work_item->TaskMain(task, worker_source, work_item, task_arena, thread_args);
        FinishTask(worker_source, task);
    }
    return rcode;
}

/// @summary Construct a list of waitable event handles that can be used to sleep a thread until termination time or a steal signal.
/// @param wait_list The list of waitable event handles to populate.
/// @param wait_source The list of TASK_SOURCE pointers associated with the waitable events in wait_list.
/// @param max_wait_handles The maximum number of handles to write to the wait_list.
/// @param source The TASK_SOURCE associated with the worker.
/// @return The number of waitable event handles written to the wait_list.
internal_function uint32_t
BuildWorkerWaitList
(
    HANDLE             *wait_list, 
    TASK_SOURCE     **wait_source,
    uint32_t     max_wait_handles, 
    TASK_SOURCE           *source
)
{   // only include handles in the source group.
    uint32_t written    =  0;
    uint32_t base_index = (uint32_t) source->GroupIndex * 64;
    uint32_t group_size = (uint32_t) source->SourceGroupSize;
    for (uint32_t i = base_index, last = base_index + group_size; i < last && written < max_wait_handles; ++i)
    {   // don't include the source in the list of waitable items.
        if (source != &source->TaskSources[i])
        {
            wait_source[written] =&source->TaskSources[i];
            wait_list  [written] = source->TaskSources[i].StealSignal;
            written++;
        }
    }
    // TODO(rlk): probably want to limit to nodes in the same NUMA group or something.
    return written;
}

/// @summary Implements the entry point of an asynchronous task worker thread.
/// @param argp Pointer to the ASYNC_WORKER state associated with the thread.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
AsyncWorkerMain
(
    void *argp
)
{
    ASYNC_WORKER        *thread_args = (ASYNC_WORKER*) argp;
    WIN32_THREAD_ARGS     *main_args =  thread_args->MainThreadArgs;
    HANDLE            init_signal[4] =
    { 
        thread_args->StartSignal ,
        thread_args->ErrorSignal ,
        thread_args->HaltSignal  ,
        main_args->TerminateEvent 
    };

    // TODO(rlk): any thread-local initialization.

    // signal to the scheduler that this worker thread is ready to go.
    SetEvent(thread_args->ReadySignal);

    // wait for a signal to start looking for work, or to terminate early.
    if (WaitForMultipleObjects(4, init_signal, FALSE, INFINITE) != WAIT_OBJECT_0)
    {   // either thread exit was signaled, or an error occurred while waiting.
        goto terminate_worker;
    }

    // TODO(rlk): the main loop of the async worker thread.

terminate_worker:
    ConsoleOutput("Async worker thread %u terminated.\n", thread_args->WorkerIndex);
    return 0;
}

/// @summary Implements the entry point of an asynchronous task worker thread.
/// @param argp Pointer to the COMPUTE_WORKER state associated with the thread.
/// @return The thread exit code (unused).
internal_function unsigned int __cdecl
ComputeWorkerMain
(
    void *argp
)
{
    COMPUTE_WORKER      *thread_args  = (COMPUTE_WORKER*) argp;
    WIN32_THREAD_ARGS     *main_args  =  thread_args->MainThreadArgs;
    TASK_SOURCE       *worker_source  =  thread_args->ThreadSource;
    TASK_SOURCE      *wait_source[64] = {};
    HANDLE            wait_signal[64] = {};
    HANDLE            init_signal[4]  =
    { 
        thread_args->StartSignal ,
        thread_args->ErrorSignal ,
        thread_args->HaltSignal  ,
        main_args->TerminateEvent 
    };

    // TODO(rlk): any thread-local initialization.

    // signal to the scheduler that this worker thread is ready to go.
    SetEvent(thread_args->ReadySignal);

    // wait for a signal to start looking for work, or to terminate early.
    if (WaitForMultipleObjects(4, init_signal, FALSE, INFINITE) != WAIT_OBJECT_0)
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
            task_id_t task = TaskQueueSteal(&wait_source[result - WAIT_OBJECT_0]->WorkQueue);
            while (task != INVALID_TASK_ID)
            {   // execute the task this thread just took or stole.
                ExecuteComputeTask(task, worker_source, &thread_args->ThreadArena, main_args);
                // that task may have generated additional work.
                // keep working as long as we can grab work to do.
                if ((task = TaskQueueTake(&worker_source->WorkQueue)) == INVALID_TASK_ID)
                {   // nothing left in the local queue, try to steal from the source that woke us.
                    task = TaskQueueSteal(&wait_source[result - WAIT_OBJECT_0]->WorkQueue);
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
/// @summary Attempt to spawn a new asynchronous task worker thread.
/// @param scheduler The scheduler instance that is spawning the worker thread.
/// @param main_args Global state created and managed by main thread and available to all threads.
/// @return true if the worker was spawned, or false if an error occurred.
internal_function bool
SpawnAsyncWorker
(
    WIN32_ASYNC_TASK_SCHEDULER *scheduler, 
    WIN32_THREAD_ARGS          *main_args
)
{
    if (scheduler->ThreadCount == scheduler->MaxThreads)
    {   // no additional worker threads can be spawned.
        return false;
    }

    unsigned int     thread_id = 0;
    HANDLE       thread_handle = NULL;
    uint32_t      worker_index =(uint32_t) scheduler->ThreadCount;
    ASYNC_WORKER *worker_state =&scheduler->WorkerState[scheduler->ThreadCount];
    HANDLE        worker_ready = CreateEvent(NULL, TRUE, FALSE, NULL);

    // initialize the worker state:
    worker_state->MainThreadArgs = main_args;
    worker_state->ReadySignal    = worker_ready;
    worker_state->StartSignal    = scheduler->StartSignal;
    worker_state->ErrorSignal    = scheduler->ErrorSignal;
    worker_state->HaltSignal     = scheduler->HaltSignal;
    worker_state->WorkerIndex    = worker_index;
    // ...

    // spawn the thread, and wait for it to report that it's ready.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, AsyncWorkerMain, worker_state, 0, &thread_id)) == 0)
    {   // unable to spawn the thread. let the caller decide if they want to exit.
        CloseHandle(worker_ready);
        return false;
    }

    // wait for the worker to report that it's ready to run.
    HANDLE   ready_signals[3] = { worker_ready , scheduler->ErrorSignal, scheduler->HaltSignal };
    if (WaitForMultipleObjects(3, ready_signals, FALSE, INFINITE) == WAIT_OBJECT_0)
    {   // the worker thread reported that it's ready. save the data.
        scheduler->OSThreadIds   [worker_index] = thread_id;
        scheduler->OSThreadHandle[worker_index] = thread_handle;
        scheduler->ThreadCount++;
        return true;
    }
    else
    {   // the worker thread reported an error and failed to initialize.
        CloseHandle(worker_ready);
        return false;
    }
}

/// @summary Attempt to spawn a new compute task worker thread.
/// @param config The scheduler configuration returned by CheckSchedulerConfiguration.
/// @param scheduler The scheduler instance that is spawning the worker thread.
/// @param total_threads The total number of threads managed by the scheduler, including not-yet-spawned threads.
/// @param main_args Global state created and managed by main thread and available to all threads.
/// @param arena The arena from which scheduler memory is allocated.
/// @return true if the worker was spawned, or false if an error occurred.
internal_function bool
SpawnComputeWorker
(
    TASK_SCHEDULER_CONFIG const        *config,
    WIN32_COMPUTE_TASK_SCHEDULER    *scheduler, 
    WIN32_THREAD_ARGS               *main_args,
    MEMORY_ARENA                        *arena
)
{
    size_t            mem_marker = ArenaMarker(arena);
    size_t          thread_index = scheduler->ThreadCount;
    COMPUTE_WORKER *worker_state =&scheduler->WorkerState[scheduler->ThreadCount];
    HANDLE          worker_ready = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE         thread_handle = NULL;
    unsigned int       thread_id = 0;

    // allocate OS memory for the thread-local memory arena.
    // this arena will be cleared after each task finishes execution.
    size_t arena_size      = config->MaxTaskArenaSize;
    size_t arena_alignment = std::alignment_of<void*>::value;
    if (CreateMemoryArena(&worker_state->OSThreadArena, arena_size) < 0)
    {   // unable to reserve the required process address space.
        CloseHandle(worker_ready);
        return false;
    }
    if (CreateArena(&worker_state->ThreadArena, arena_size, arena_alignment, &worker_state->OSThreadArena) < 0)
    {   // unable to commit the required process address space.
        goto cleanup_and_fail;
    }

    // initialize the worker state:
    worker_state->MainThreadArgs = main_args;
    worker_state->ReadySignal    = worker_ready;
    worker_state->StartSignal    = scheduler->StartSignal;
    worker_state->ErrorSignal    = scheduler->ErrorSignal;
    worker_state->HaltSignal     = scheduler->HaltSignal;
    worker_state->ThreadIndex    = scheduler->ThreadCount;
    worker_state->ThreadSource   = NewTaskSource(scheduler, arena, 0, 0);

    // spawn the thread, and wait for it to report that it's ready.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, ComputeWorkerMain, worker_state, 0, &thread_id)) == 0)
    {   // unable to spawn the thread. let the caller decide if they want to exit.
        goto cleanup_and_fail;
    }

    // wait for the worker to report that it's ready to run.
    HANDLE   ready_signals[3] = { worker_ready , scheduler->ErrorSignal, scheduler->HaltSignal };
    if (WaitForMultipleObjects(3, ready_signals, FALSE, INFINITE) == WAIT_OBJECT_0)
    {   // the worker thread reported that it's ready. save the data.
        scheduler->OSThreadIds   [thread_index] = thread_id;
        scheduler->OSThreadHandle[thread_index] = thread_handle;
        scheduler->ThreadCount++;
        return true;
    }
    else
    {   // the worker thread reported an error and failed to initialize.
        goto cleanup_and_fail;
    }

cleanup_and_fail:
    DeleteMemoryArena(&worker_state->OSThreadArena);
    ArenaResetToMarker(arena, mem_marker);
    CloseHandle(worker_ready);
    return false;
}

/// @summary Calculate the amount of memory required for a given scheduler configuration.
/// @param config The scheduler configuration returned from the CheckSchedulerConfiguration function.
/// @return The number of bytes required to create an asynchronous task scheduler of the specified type with the given configuration.
internal_function size_t
CalculateMemoryForAsyncScheduler
(
    TASK_SCHEDULER_CONFIG *config
)
{
    size_t  size = 0;
    // account for the size of the variable-length data arrays.
    size += AllocationSizeForArray<unsigned int>(config->MaxWorkerThreads);
    size += AllocationSizeForArray<HANDLE      >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<ASYNC_WORKER>(config->MaxWorkerThreads);
    // ...
    return size;
}

/// @summary Calculate the amount of memory required for a given scheduler configuration.
/// @param config The scheduler configuration returned from the CheckSchedulerConfiguration function.
/// @param max_sources The maximum number of task sources that can be registered with the scheduler.
/// @return The number of bytes required to create a compute task scheduler of the specified type with the given configuration.
internal_function size_t
CalculateMemoryForComputeScheduler
(
    TASK_SCHEDULER_CONFIG     *config, 
    size_t                max_sources
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
    size += AllocationSizeForArray<unsigned int  >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<HANDLE        >(config->MaxWorkerThreads);
    size += AllocationSizeForArray<COMPUTE_WORKER>(config->MaxWorkerThreads);
    size += AllocationSizeForArray<TASK_SOURCE   >(max_sources);
    // account for the size of the minimum number of task sources.
    for (size_t i = 0, n = config->MaxWorkerThreads + 1; i < n; ++i)
    {   // the main thread and worker threads always use the scheduler configuration.
        size += CalculateMemoryForTaskSource(config->MaxActiveTicks, config->MaxTasksPerTick);
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
DefineTask
(
    TASK_SOURCE     *source,
    TASK_ENTRY    task_main, 
    void   const *task_args, 
    size_t const  args_size, 
    task_id_t     parent_id, 
    task_id_t     wait_task
)
{
    if (args_size > WORK_ITEM::MAX_DATA)
    {   // there's too much data being passed. the caller should allocate storage elsewhere and pass us the pointer.
        ConsoleError("ERROR: Argument data too large when defining task (parent %08X). Passing %zu bytes, max is %zu bytes.\n", parent_id, args_size, WORK_ITEM::MAX_DATA); 
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

    int32_t   *dep_wcount;
    uint32_t buffer_index = source->BufferIndex;
    uint32_t   task_index = source->TaskCount++;
    task_id_t     task_id = MakeTaskId(buffer_index, SCHEDULER_TYPE_COMPUTE, task_index, source->SourceIndex);
    int32_t   &work_count = source->WorkCounts[buffer_index][task_index];
    WORK_ITEM &work_item  = source->WorkItems [buffer_index][task_index];
    PERMITS_LIST &permit  = source->PermitList[buffer_index][task_index];

    work_item.Reserved0   = 0;
    work_item.ParentTask  = parent_id;
    work_item.TaskMain    = task_main;
    if (task_args != NULL && args_size > 0)
    {   // we could first zero the work_item.TaskArgs memory.
        CopyMemory(work_item.TaskArgs, task_args, args_size);
    }
    permit.Count = 0;
    work_count   = 2; // decremented in FinishTask.

    if (IsValidTask(wait_task) && (dep_wcount = GetTaskWorkCount(wait_task, source->TaskSources)) != NULL)
    {   // determine whether the dependency task has been completed.
        PERMITS_LIST *p = GetTaskPermitsList(wait_task, source->TaskSources);
        if (InterlockedAdd((volatile LONG*) dep_wcount, 0) > 0)
        {   // the dependency has not been completed, so update the permits list of the dependency.
            // the permits list for wait_task could be accessed concurrently by other threads:
            // - another thread could be executing DefineTask with the same wait_task.
            // - another thread could be executing FinishTask for wait_task.
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
                    TaskQueuePush(&source->WorkQueue, task_id);
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
    TaskQueuePush(&source->WorkQueue, task_id);
    return task_id;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
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
/// @param arena The memory arena to allocate from.
/// @param max_active_ticks The maximum number of ticks in-flight at any given time. Specify 0 to inherit the scheduler default.
/// @param max_tasks_per_tick The maximum number of tasks that can be spawned during any single tick. Specify 0 to inherit the scheduler default.
/// @return A pointer to the initialize TASK_SOURCE, or NULL.
public_function TASK_SOURCE*
NewTaskSource
(
    WIN32_COMPUTE_TASK_SCHEDULER         *scheduler, 
    MEMORY_ARENA                             *arena, 
    size_t                         max_active_ticks, 
    size_t                       max_tasks_per_tick
)
{
    if (max_active_ticks == 0)
    {   // inherit the value from the scheduler.
        max_active_ticks = scheduler->MaxTicksInFlight;
    }
    if (max_tasks_per_tick == 0)
    {   // inherit the value from the scheduler.
        max_tasks_per_tick = scheduler->MaxTasksPerTick;
    }
    if (max_active_ticks > MAX_TICKS_IN_FLIGHT)
    {   // consider this to be an error; it's easily trapped during development.
        return NULL;
    }
    if ((max_tasks_per_tick & (max_tasks_per_tick - 1)) != 0)
    {   // this value must be a power-of-two. round up to the next multiple.
        size_t n = 1;
        size_t m = max_tasks_per_tick;
        while (n < m)
        {
            n <<= 1;
        }
        max_tasks_per_tick = n;
    }
    if (max_tasks_per_tick > MAX_TASKS_PER_TICK)
    {   // consider this to be an error; it's easily trapped during development.
        return NULL;
    }
    if (scheduler->SourceCount == scheduler->MaxSourceCount)
    {   // no additional sources can be allocated from the scheduler.
        return NULL;
    }
    size_t bytes_needed = CalculateMemoryForTaskSource(max_active_ticks, max_tasks_per_tick);
    size_t alignment    = std::alignment_of<void*>::value;
    if (!ArenaCanAllocate(arena, bytes_needed, alignment))
    {   // the arena doesn't have sufficient memory to initialize a source with the requested attributes.
        return NULL;
    }

    size_t const  PER_GROUP  =  64;
    size_t       num_groups  =((scheduler->MaxSourceCount - 1) / PER_GROUP) + 1;
    size_t        remaining  =  scheduler->MaxSourceCount - (PER_GROUP * (num_groups - 1));
    size_t       last_group  =  num_groups - 1;
    size_t       this_group  =  scheduler->SourceCount / PER_GROUP;
    TASK_SOURCE     *source  = &scheduler->SourceList[scheduler->SourceCount];
    CreateTaskQueue(&source->WorkQueue, max_tasks_per_tick, arena);
    source->StealSignal      = CreateEvent(NULL, FALSE, FALSE, NULL); // auto-reset
    source->GroupIndex       = this_group;
    source->SourceIndex      =(uint32_t)  scheduler->SourceCount;
    source->SourceGroupSize  =(uint32_t)((this_group == last_group) ? remaining : PER_GROUP);
    source->MaxTicksInFlight = max_active_ticks;
    source->MaxTasksPerTick  = max_tasks_per_tick;
    source->TaskSourceCount  = scheduler->MaxSourceCount;
    source->TaskSources      = scheduler->SourceList;
    source->BufferIndex      = 0;
    source->TaskCount        = 0;
    for (size_t i = 0, n = max_active_ticks; i < n; ++i)
    {
        source->WorkItems [i] = PushArray<WORK_ITEM   >(arena, max_tasks_per_tick);
        source->WorkCounts[i] = PushArray<int32_t     >(arena, max_tasks_per_tick);
        source->PermitList[i] = PushArray<PERMITS_LIST>(arena, max_tasks_per_tick);
    }
    scheduler->SourceCount++;
    return source;
}

/// @summary Create a new asynchronous task scheduler instance. The scheduler worker threads are launched separately.
/// @param config The scheduler configuration.
/// @param main_args The global data managed by the main thread and passed to all worker threads.
/// @param arena The memory arena used to allocate scheduler memory. Per-worker memory is allocated directly from the OS.
/// @return Zero if the scheduler is successfully initialized, or -1 if an error occurred.
public_function int
CreateAsyncScheduler
(
    WIN32_ASYNC_TASK_SCHEDULER *scheduler,
    TASK_SCHEDULER_CONFIG         *config, 
    WIN32_THREAD_ARGS          *main_args,
    MEMORY_ARENA                   *arena
)
{
    TASK_SCHEDULER_CONFIG valid_config = {};
    WIN32_CPU_INFO           *cpu_info = main_args->HostCPUInfo;
    size_t                   alignment = std::alignment_of<WIN32_ASYNC_TASK_SCHEDULER>::value;
    
    CheckSchedulerConfiguration(&valid_config, config, cpu_info, SCHEDULER_TYPE_ASYNC);
    size_t expected_size = CalculateMemoryForAsyncScheduler(&valid_config);
    if (!ArenaCanAllocate(arena, expected_size, alignment))
    {   // there's not enough memory for the core scheduler and worker data.
        return -1;
    }

    size_t mem_mark = ArenaMarker(arena);
    HANDLE ev_error = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE ev_start = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE ev_halt  = CreateEvent(NULL, TRUE, FALSE, NULL);

    ZeroMemory(scheduler, sizeof(WIN32_ASYNC_TASK_SCHEDULER));
    scheduler->ErrorSignal    = ev_error;
    scheduler->StartSignal    = ev_start;
    scheduler->HaltSignal     = ev_halt;
    scheduler->MaxThreads     = valid_config.MaxWorkerThreads;
    scheduler->ThreadCount    = 0;
    scheduler->OSThreadIds    = PushArray<unsigned int>(arena, valid_config.MaxWorkerThreads);
    scheduler->OSThreadHandle = PushArray<HANDLE      >(arena, valid_config.MaxWorkerThreads);
    scheduler->WorkerState    = PushArray<ASYNC_WORKER>(arena, valid_config.MaxWorkerThreads);
    ZeroMemory(scheduler->OSThreadIds   , valid_config.MaxWorkerThreads * sizeof(unsigned int));
    ZeroMemory(scheduler->OSThreadHandle, valid_config.MaxWorkerThreads * sizeof(HANDLE));
    ZeroMemory(scheduler->WorkerState   , valid_config.MaxWorkerThreads * sizeof(ASYNC_WORKER));

    size_t spawn_count = SCHEDULER_MIN(valid_config.MaxWorkerThreads, cpu_info->HardwareThreads);
    for (size_t i = 0; i < spawn_count; ++i)
    {
        if (!SpawnAsyncWorker(scheduler, main_args))
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

/// @summary Notify all asynchronous task scheduler worker threads to start monitoring work queues.
/// @param scheduler The task scheduler to launch.
public_function void
LaunchScheduler
(
    WIN32_ASYNC_TASK_SCHEDULER *scheduler
)
{
    SetEvent(scheduler->StartSignal);
}

/// @summary Notify all asynchronous task scheduler worker threads to shutdown.
/// @param scheduler The task scheduler to halt.
public_function void
HaltScheduler
(
    WIN32_ASYNC_TASK_SCHEDULER *scheduler
)
{
    if (scheduler->ThreadCount > 0)
    {   // signal all worker threads to exit, and wait for them.
        SetEvent(scheduler->HaltSignal);
        WaitForMultipleObjects((DWORD) scheduler->ThreadCount, scheduler->OSThreadHandle, TRUE, INFINITE);
    }
}

/// @summary Create a new asynchronous task scheduler instance. The scheduler worker threads are launched separately.
/// @param scheduler The scheduler instance to initialize.
/// @param config The scheduler configuration.
/// @param main_args The global data managed by the main thread and passed to all worker threads.
/// @param arena The memory arena used to allocate scheduler memory. Per-worker memory is allocated directly from the OS.
/// @param max_sources The maximum number of task sources. The minimum value is the number of worker threads plus one for the main thread.
/// @return A pointer to the new scheduler instance, or NULL.
public_function int
CreateComputeScheduler
(
    WIN32_COMPUTE_TASK_SCHEDULER *scheduler, 
    TASK_SCHEDULER_CONFIG           *config, 
    WIN32_THREAD_ARGS            *main_args,
    MEMORY_ARENA                     *arena, 
    size_t                      max_sources
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
    scheduler->SourceList       = PushArray<TASK_SOURCE   >(arena, max_sources);
    ZeroMemory(scheduler->OSThreadIds   , valid_config.MaxWorkerThreads * sizeof(unsigned int));
    ZeroMemory(scheduler->OSThreadHandle, valid_config.MaxWorkerThreads * sizeof(HANDLE));
    ZeroMemory(scheduler->WorkerState   , valid_config.MaxWorkerThreads * sizeof(COMPUTE_WORKER));
    ZeroMemory(scheduler->SourceList    , max_sources                   * sizeof(TASK_SOURCE));

    // always allocate source 0 to the main thread.
    NewTaskSource(scheduler, arena, 0, 0);

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

/// @summary Notify all compute task scheduler worker threads to start monitoring work queues.
/// @param scheduler The task scheduler to launch.
public_function void
LaunchScheduler
(
    WIN32_COMPUTE_TASK_SCHEDULER *scheduler
)
{
    SetEvent(scheduler->StartSignal);
}

/// @summary Notify all compute task scheduler worker threads to shutdown.
/// @param scheduler The task scheduler to halt.
public_function void
HaltScheduler
(
    WIN32_COMPUTE_TASK_SCHEDULER *scheduler
)
{
    if (scheduler->ThreadCount > 0)
    {   // signal all worker threads to exit, and wait for them.
        SetEvent(scheduler->HaltSignal);
        WaitForMultipleObjects((DWORD) scheduler->ThreadCount, scheduler->OSThreadHandle, TRUE, INFINITE);
    }
}

/// @summary Wake exactly one waiting worker thread if there's work it could steal. Call this if several items are added to a work queue.
/// @param source The TASK_SOURCE owned by the calling thread.
public_function void
SignalWaitingWorkers
(
    TASK_SOURCE *source
)
{
    SetEvent(source->StealSignal);
}

/// @summary Retrieve the task source for the root thread. This function should only ever be called from the root thread (the thread that submits the root tasks.)
/// @param scheduler The scheduler instance to query.
/// @return A pointer to the worker state for the root thread, which can be used to spawn root tasks.
public_function inline TASK_SOURCE*
GetRootTaskSource
(
    WIN32_COMPUTE_TASK_SCHEDULER *scheduler
)
{
    return &scheduler->SourceList[0];
}

/// @summary Spawn a new task. After spawning any child tasks, call FinishTask to complete the task definition.
/// @param source The TASK_SOURCE for the calling thread. 
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @param wait_task The identifier of the task that must complete prior to executing this new task, or INVALID_TASK_ID.
/// @return The identifier of the new task, or INVALID_TASK_ID.
public_function inline task_id_t
NewTask
(
    TASK_SOURCE       *source, 
    TASK_ENTRY      task_main, 
    void   const   *task_args, 
    size_t const    args_size,
    task_id_t       wait_task = INVALID_TASK_ID
)
{
    return DefineTask(source, task_main, task_args, args_size, INVALID_TASK_ID, wait_task);
}

/// @summary Spawn a new child task. Call FinishTask to complete the task definition.
/// @param source The TASK_SOURCE for the calling thread. 
/// @param task_main The entry point of the task.
/// @param task_args User-supplied argument data for the task instance. This data is copied into the task.
/// @param args_size The number of bytes of argument data to copy into the task.
/// @param parent_id The task ID of the parent task, as returned by the NewTask function used to create the parent task.
/// @param wait_task The identifier of the task that must complete prior to executing this new task, or INVALID_TASK_ID.
/// @return The identifier of the new task, or INVALID_TASK_ID.
public_function task_id_t
NewChildTask
(
    TASK_SOURCE     *source, 
    TASK_ENTRY    task_main, 
    void   const *task_args, 
    size_t const  args_size, 
    task_id_t     parent_id, 
    task_id_t     wait_task = INVALID_TASK_ID
)
{
    WORK_ITEM *parent_task;  // read-only
    int32_t   *parent_count; // write-only
    if (IsValidTask(parent_id))
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

    return DefineTask(source, task_main, task_args, args_size, parent_id, wait_task);
}

/// @summary Indicate that a task (including any children) has been completely defined and allow it to finish execution.
/// @param source The source that defined the task. This should be the same source that was passed to NewTask.
/// @param task The task ID returned by the NewTask call.
/// @return The number of tasks added to the work queue of the calling thread.
public_function int32_t
FinishTask
(
    TASK_SOURCE *source,
    task_id_t      task
)
{
    int32_t *work_count;
    if (IsValidTask(task) && (work_count = GetTaskWorkCount(task, source->TaskSources)) != NULL)
    {
        WORK_ITEM *work_item = GetTaskWorkItem(task, source->TaskSources);
        if (InterlockedDecrement((volatile LONG*) work_count) <= 0)
        {   // the work item has finished executing. this may permit other tasks to run.
            PERMITS_LIST  *p = GetTaskPermitsList(task, source->TaskSources);
            int32_t n = InterlockedExchange((volatile LONG*) &p->Count, -1);
            if (n > 0)
            {   // add the now-permitted tasks to the work queue of the calling thread.
                for (int32_t i = 0; i < n; ++i)
                {
                    TaskQueuePush(&source->WorkQueue, p->Tasks[i]);
                }
            }
            // decrement the work counter on any parent task.
            n += FinishTask(source, work_item->ParentTask);
            if (n > 0)
            {   // wake up idle threads to help work.
                SignalWaitingWorkers(source);
            }
            return n;
        }
    }
    return 0;
}

