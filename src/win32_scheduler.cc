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
#define TASK_ID_MASK_TICK_P               (0x00000003UL)
#define TASK_ID_MASK_TICK_U               (0x00000003UL)
#define TASK_ID_MASK_TYPE_P               (0x00000004UL)
#define TASK_ID_MASK_TYPE_U               (0x00000001UL)
#define TASK_ID_MASK_INDEX_P              (0x0007FFF8UL)
#define TASK_ID_MASK_INDEX_U              (0x0000FFFFUL)
#define TASK_ID_MASK_THREAD_P             (0x7FF80000UL)
#define TASK_ID_MASK_THREAD_U             (0x00000FFFUL)
#define TASK_ID_MASK_VALID_P              (0x80000000UL)
#define TASK_ID_MASK_VALID_U              (0x00000001UL)
#define TASK_ID_SHIFT_TICK                (0)
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
    uint32_t       ValidTask;        /// One of TASK_ID_TYPE specifying whether the task is valid.
    uint32_t       SchedulerType;    /// One of SCHEDULER_TYPE specifying the scheduler that owns the task.
    uint32_t       ThreadIndex;      /// The zero-based index of the thread that defines the task.
    uint32_t       BufferIndex;      /// The zero-based index of the thread-local buffer that defines the task.
    uint32_t       TaskIndex;        /// The zero-based index of the task within the thread-local buffer.
};

/// @summary Define a structure used to specify data used to configure a task scheduler instance at creation time.
struct TASK_SCHEDULER_CONFIG
{
    size_t         MaxActiveTicks;   /// The maximum number of application ticks in-flight at any given time.
    size_t         MaxWorkerThreads; /// The maximum number of worker threads that can be spawned.
    size_t         MaxTasksPerTick;  /// The maximum number of tasks that can be created during a single application tick.
    size_t         MaxTaskArenaSize; /// The number of bytes to allocate for each thread-local arena.
};

/// @summary Define the data associated with a work item.
struct WORK_ITEM
{
    TASK_ENTRY     TaskMain;         /// The task entry point.
};

/// @summary Define the data associated with the list of tasks that are waiting to run.
struct WTR_TASK_LIST
{
    size_t         TaskCount;        /// The number of tasks in the waiting-to-run list.
    task_id_t     *DependencyList;   /// The task ID of the task that must be completed before the waiting task can be launched.
    uint16_t      *TaskIndexList;    /// The zero-based index of the task WORK_ITEM within the task buffer.
};

/// @summary Define the data associated with task definitions for a single in-flight tick.
struct TASK_BUFFER
{
    size_t         TaskCount;        /// The number of tasks allocated from the buffer.
    WORK_ITEM     *WorkItems;        /// Fixed-length storage for storing data associated with each task.
    int32_t       *WorkRemaining;    /// Fixed-length storage for counters tracking the outstanding work for each task.
};

/// @summary Define the data associated with a single worker thread.
struct TASK_WORKER
{
    size_t         MaxTasksPerTick;  /// The maximum number of tasks that can be created in a given tick.
    MEMORY_ARENA   ThreadArena;      /// The thread-local memory arena.
    TASK_BUFFER    TaskList[4];      /// The per-tick data used to track tasks created on that tick.
    // TODO(rlk):  WorkQueue
    // TODO(rlk):  WTR list
};

/// @summary Define the data associated with an asynchronous task scheduler.
struct WIN32_ASYNC_TASK_SCHEDULER
{
    HANDLE         ErrorSignal;      /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE         LaunchSignal;     /// Manual-reset event signaled when worker threads should start running tasks.

    size_t         ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int  *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE        *OSThreadHandle;   /// The operating system thread handle for each worker thread.
};

/// @summary Define the data associated with a compute-oriented task scheduler.
struct WIN32_COMPUTE_TASK_SCHEDULER
{
    HANDLE         ErrorSignal;      /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE         LaunchSignal;     /// Manual-reset event signaled when worker threads should start running tasks.

    size_t         ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int  *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE        *OSThreadHandle;   /// The operating system thread handle for each worker thread.
    TASK_WORKER   *WorkerThreads;    /// The state data for each worker thread.
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
    // the maximum number of tasks per-tick should always be a power of two.
    if ((dst_config->MaxTasksPerTick & (dst_config->MaxTasksPerTick-1) != 0)
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
    // TODO(rlk): Use GlobalMemoryStatusEx to query physical memory.
    // report an error if the requested memory is too high.

    return performance_ok;
}

/// @summary Calculate the amount of memory required for a given scheduler configuration.
/// @param config The scheduler configuration.
/// @param scheduler_type The type of scheduler, one of SCHEDULER_TYPE.
/// @return The number of bytes required to create a scheduler of the specified type with the given configuration, or 0 if the configuration or scheduler type is invalid.
public_function size_t
CalculateMemoryForScheduler
(
    TASK_SCHEDULER_CONFIG        *config,
    uint32_t              scheduler_type, 
    size_t                    &alignment
)
{
    if (scheduler_type == SCHEDULER_TYPE_ASYNC)
    {
        size_t   align  = std::alignment_of<HANDLE>::value;
        size_t    size  = sizeof(HANDLE) * 2; // ErrorSignal, LaunchSignal
    }
    if (scheduler_type == SCHEDULER_TYPE_COMPUTE)
    {
    }
    // else, unknown scheduler type.
    return 0;
}

/*
struct TASK_SCHEDULER_CONFIG
{
    size_t         MaxActiveTicks;   /// The maximum number of application ticks in-flight at any given time.
    size_t         MaxWorkerThreads; /// The maximum number of worker threads that can be spawned.
    size_t         MaxTasksPerTick;  /// The maximum number of tasks that can be created during a single application tick.
    size_t         MaxTaskArenaSize; /// The number of bytes to allocate for each thread-local arena.
};
    HANDLE         ErrorSignal;      /// Manual-reset event used by worker threads to signal a fatal error.
    HANDLE         LaunchSignal;     /// Manual-reset event signaled when worker threads should start running tasks.

    size_t         ThreadCount;      /// The number of worker threads managed by the scheduler.
    unsigned int  *OSThreadIds;      /// The operating system identifiers for each worker thread.
    HANDLE        *OSThreadHandle;   /// The operating system thread handle for each worker thread.
*/

public_function WIN32_ASYNC_TASK_SCHEDULER*
CreateAsyncScheduler
(
    TASK_SCHEDULER_CONFIG *config, 
    MEMORY_ARENA           *arena
)
{   // TODO(rlk): validate the configuration.
    HANDLE                       ev_error = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE                       ev_start = CreateEvent(NULL, TRUE, FALSE, NULL);
    WIN32_ASYNC_TASK_SCHEDULER *scheduler = PushStruct<WIN32_ASYNC_TASK_SCHEDULER>(arena);
    if (scheduler == NULL) return NULL;

    //!ArenaCanAllocate(arena, alignment)

cleanup_and_fail:
    if (ev_start != NULL) CloseHandle(ev_start);
    if (ev_error != NULL) CloseHandle(ev_error);
}

public_function WIN32_COMPUTE_TASK_SCHEDULER*
CreateComputeScheduler
(
    TASK_SCHEDULER_CONFIG *config, 
    MEMORY_ARENA           *arena
)
{
}

public_function task_id_t
CreateComputeTask
(
    WIN32_COMPUTE_TASK_SCHEDULER  *scheduler, 
    TASK_ENTRY                   entry_point, 
    task_id_t                         parent,
    task_id_t                     dependency
)
{
}

public_function void
ReadyComputeTask
(
    WIN32_COMPUTE_TASK_SCHEDULER *scheduler,
    task_id_t                          task
)
{
}

