/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement a task scheduler for non-blocking tasks. Blocking 
/// operations such as network and database calls should use the async 
/// scheduler instead, which maintains it own pool of threads.
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary The value representing a 'null' task ID.
#ifndef WIN32_TASK_ID_NONE
#define WIN32_TASK_ID_NONE          0x00000000UL
#endif

// Handle Layout:
// I = object ID  (16 bits)
// G = generation  (8 bits)
// T = object type (8 bits)
// 31                              0
//  ................................
//  TTTTGGGGGGGGGGGGIIIIIIIIIIIIIIII
// 
// TASK_HANDLE_MASK_x_P => Mask in a packed value
// TASK_HANDLE_MASK_x_U => Mask in an unpacked value
#define TASK_HANDLE_MASK_I_P     (0x0000FFFFUL)
#define TASK_HANDLE_MASK_I_U     (0x0000FFFFUL)
#define TASK_HANDLE_MASK_G_P     (0x0FFF0000UL)
#define TASK_HANDLE_MASK_G_U     (0x00000FFFUL)
#define TASK_HANDLE_MASK_T_P     (0xF0000000UL)
#define TASK_HANDLE_MASK_T_U     (0x0000000FUL)
#define TASK_HANDLE_SHIFT_I      (0)
#define TASK_HANDLE_SHIFT_G      (16)
#define TASK_HANDLE_SHIFT_T      (28)
#define INVALID_TASK_HANDLE      ((task_handle_t) 0)

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Use a "unique" 32-bit integer to identify a task.
typedef uint32_t task_handle_t;

/// @summary Define the signature of the callback function invoked to execute a task.
typedef int (*WIN32_TASK_ENTRYPOINT)(struct TASK_ARGS *);

/// @summary Define the result codes returned when trying to retrieve an item from a task queue via a take or steal operation.
enum WIN32_TASK_QUEUE_GET_RESULT : int
{
    WIN32_TASK_QUEUE_GET_SUCCESS = 0,     /// An item was retrieved successfully.
    WIN32_TASK_QUEUE_GET_EMPTY   = 1,     /// No item was retrieved because the queue is empty.
    WIN32_TASK_QUEUE_GET_ABORT   = 2,     /// No item was retrieved because of concurrent access. Try again.
};

/// @summary Define the types of tasks available in the system.
enum WIN32_TASK_TYPE : uint32_t
{
    WIN32_TASK_TYPE_BLOCKING     = 1,     /// The task is a blocking, asynchronous operation.
    WIN32_TASK_TYPE_NON_BLOCKING = 2,     /// The task is a non-blocking, compute-oriented operation.
};

/// @summary Describes the location of a task within a task handle table.
struct TASK_INDEX
{
    uint32_t              Id;             /// The index-in-table + generation.
    uint16_t              Next;           /// The zero-based index of the next free slot.
    uint16_t              Index;          /// The zero-based index into the tightly-packed task array.
};

/// @summary Define the data passed to all threads in the task scheduler thread pool.
struct WIN32_TASK_THREAD_ARGS
{
    WIN32_THREAD_ARGS    *MainThreadArgs; /// Pointer to the global data from the main thread.
    HANDLE                ReadySignal;    /// Event used to signal that the worker thread is ready to go.
    HANDLE                StartSignal;    /// Event used to signal all threads to start working.
    HANDLE                ErrorSignal;    /// Event used to signal that the worker thread encountered a fatal error.
    uint32_t              InternalId;     /// The internal identifier of the worker thread within the pool.
    // ...
};

/// @summary Define the internal representation of a work item within the global task list.
struct WIN32_TASK_WORKITEM
{
    WIN32_TASK_ENTRYPOINT TaskEntry;      /// The task entry point.
    void                 *TaskArgs;       /// Task-specific data associated with the work item.
    uint32_t              TaskId;         /// The internal task identifier.
};

/// @summary Define the internal data associated with a live task.
struct WIN32_TASK
{   static size_t const   MAX_DEPENDENTS    = 10;
    static size_t const   N                 = MAX_DEPENDENTS;
    WIN32_TASK_ENTRYPOINT TaskEntry;      /// The task entry point.
    void                 *TaskArgs;       /// Task-specific data associated with the work item.
    task_handle_t         Parent;         /// The unique identifier of the parent task.
    uint32_t              DependentCount; /// The number of dependents associated with the task.
    task_handle_t         Dependents[N];  /// The unique identifier of the task this task depends on.
};

/// @summary Define the data associated with a fixed-size task queue. The task queue is a double-ended queue that supports work stealing based on:
/// http://www.di.ens.fr/~zappa/readings/ppopp13.pdf
struct struct_alignment(CACHELINE_SIZE) WIN32_TASK_QUEUE
{   static size_t   const ALIGNMENT         = CACHELINE_SIZE;
    static size_t   const PAD_SIZE          = CACHELINE_SIZE - sizeof(std::atomic<uint32_t>);
    static uint32_t const MAX_TASKS         = 16384; /// The maximum number of active tasks. Must be a power of two.
    static uint32_t const MASK              = MAX_TASKS - 1;
    std::atomic<uint32_t> Top;            /// The public end of the deque, updated by steal operations.
    uint8_t               Pad0[PAD_SIZE]; /// Padding to separate the public data from the private data.
    std::atomic<uint32_t> Bottom;         /// The private end of the deque, updated by push and take operations.
    uint8_t               Pad1[PAD_SIZE]; /// Padding to separate the private data from the storage data.
    WIN32_TASK            Task[MAX_TASKS];/// Thread-local storage for task data. The number of items is always a power of two.
};

/// @summary Define the data associated with a task scheduler instance.
struct WIN32_TASK_SCHEDULER
{   static size_t   const MAX_LIVE_JOBS     = 32767;
    static size_t   const JOB_CAPACITY      = MAX_LIVE_JOBS+1;
    static uint32_t const INDEX_INVALID     = MAX_LIVE_JOBS+1;
    static uint32_t const INDEX_MASK        = TASK_HANDLE_MASK_I_P;
    static uint32_t const GENERATION_NEXT   = TASK_HANDLE_MASK_I_P+1;
    static uint32_t const GENERATION_MASK   = TASK_HANDLE_MASK_G_P;
    static size_t   const N                 = JOB_CAPACITY;
    
    HANDLE                ErrorSignal;    /// Manual reset event used by workers to indicate a fatal error.
    HANDLE                LaunchSignal;   /// Manual reset event used to launch all worker threads.
    
    size_t                ThreadCount;    /// The number of worker threads in the pool.
    unsigned int         *WorkerIds;      /// The OS identifier for each worker thread.
    HANDLE               *WorkerThreads;  /// The handle of each worker thread in the pool.

    CRITICAL_SECTION      TaskTableLock;  /// Protects the global task table.
    size_t                TaskCount;      /// The number of live tasks in the global tasks table.
    uint16_t              FreeListTail;   /// The index of the most recently freed item.
    uint16_t              FreeListHead;   /// The index of the next available item.
    TASK_INDEX            Indices[N];     /// The sparse array used to look up tasks in the packed array.
    WIN32_TASK_WORKITEM   WorkItems[N];   /// The work item data for each task (packed).
    int32_t               WorkCounts[N];  /// The number of outstanding task completions before each task is considered finished (packed).
    task_handle_t         Dependencies[N];/// The handle of the dependency needed to launch each task (packed).
};

/// @summary Define the data passed to a task entry point.
struct TASK_ARGS
{
    WIN32_THREAD_ARGS    *MainThreadArgs; /// Pointer to the global data from the main thread.
    MEMORY_ARENA         *TaskArena;      /// Memory arena used to allocate working memory.
    void                 *TaskArgs;       /// Task-specific data associated with the work item.
    // ... other data
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Construct a task handle out of its constituent parts.
/// @param table_id The task identifier returned from the CreateTask function.
/// @param task_type The task type identifier. One of WIN32_TASK_TYPE.
/// @return The unique object handle.
internal_function inline task_handle_t
MakeTaskHandle
(
    uint32_t  table_id, 
    uint32_t task_type
)
{
    uint32_t I =(uint32_t(table_id ) << TASK_HANDLE_SHIFT_I) & (TASK_HANDLE_MASK_I_P | TASK_HANDLE_MASK_G_P);
    uint32_t T =(uint32_t(task_type) << TASK_HANDLE_SHIFT_T) & (TASK_HANDLE_MASK_T_P);
    return  (I | T);
}

/// @summary Construct a task handle out of its constituent parts.
/// @param array_index The zero-based index of the task in the task table.
/// @param generation The generation value for the task table slot.
/// @param task_type The task type identifier. One of WIN32_TASK_TYPE.
/// @return The unique object handle.
internal_function inline task_handle_t
MakeTaskHandle
(
    uint32_t array_index, 
    uint32_t  generation,
    uint32_t   task_type
)
{
    uint32_t I =(uint32_t(array_index) & TASK_HANDLE_MASK_I_U) << TASK_HANDLE_SHIFT_I;
    uint32_t G =(uint32_t(generation ) & TASK_HANDLE_MASK_G_U) << TASK_HANDLE_SHIFT_G;
    uint32_t T =(uint32_t(task_type  ) & TASK_HANDLE_MASK_T_U) << TASK_HANDLE_SHIFT_T;
    return  (I | G | T);
}

/// @summary Extract the task table identifier from a task handle.
/// @param handle The handle to inspect.
/// @return The table identifier.
public_function inline uint32_t
GetTaskTableId
(
    task_handle_t handle
)
{
    return (uint32_t) ((handle & (TASK_HANDLE_MASK_I_P | TASK_HANDLE_MASK_G_P)) >> TASK_HANDLE_SHIFT_I);
}

/// @summary Extract the task type identifier from a task handle.
/// @param handle The handle to inspect.
/// @return The task type identifier, one of WIN32_TASK_TYPE.
public_function inline uint32_t
GetTaskType
(
    task_handle_t handle
)
{
    return (uint32_t) ((handle & TASK_HANDLE_MASK_T_P) >> TASK_HANDLE_SHIFT_T);
}

/// @summary Wipe out the table of live tasks. Any executing tasks are allowed to complete.
/// @param scheduler The task scheduler to reset.
internal_function void
ClearSchedulerTaskTable
(
    WIN32_TASK_SCHEDULER *scheduler
)
{
    EnterCriticalSection(&scheduler->TaskTableLock);
    {
        scheduler->TaskCount    = 0;
        scheduler->FreeListTail = WIN32_TASK_SCHEDULER::JOB_CAPACITY-1;
        scheduler->FreeListHead = 0;
        for (size_t i = 0; i < WIN32_TASK_SCHEDULER::JOB_CAPACITY; ++i)
        {
            scheduler->Indices[i].Id      = uint32_t(i);
            scheduler->Indices[i].Next    = uint16_t(i+1);
            scheduler->Indices[i].Index   = WIN32_TASK_SCHEDULER::INDEX_INVALID;
        }
        ZeroMemory(scheduler->WorkItems   , WIN32_TASK_SCHEDULER::JOB_CAPACITY * sizeof(WIN32_TASK_WORKITEM));
        ZeroMemory(scheduler->WorkCounts  , WIN32_TASK_SCHEDULER::JOB_CAPACITY * sizeof(int32_t));
        ZeroMemory(scheduler->Dependencies, WIN32_TASK_SCHEDULER::JOB_CAPACITY * sizeof(task_handle_t));
    }
    LeaveCriticalSection(&scheduler->TaskTableLock);
}

/// @summary Retrieve the zero-based index into the packed task list arrays for a task given its handle. The caller is expected to hold the task table lock.
/// @param scheduler The task scheduler that owns the task.
/// @param handle The handle of the task to look up.
/// @return The zero-based index of the task in the task table, or WIN32_TASK_SCHEDULER::INDEX_INVALID.
internal_function inline size_t
IndexForTask_NoLock
(
    WIN32_TASK_SCHEDULER *scheduler, 
    task_handle_t            handle
)
{
    uint32_t   const    taskid  = GetTaskTableId(handle);
    TASK_INDEX const    &index  = scheduler->Indices[taskid & WIN32_TASK_SCHEDULER::INDEX_MASK];
    return (index.Id == taskid) ? index.Index : WIN32_TASK_SCHEDULER::INDEX_INVALID;
}

/// @summary Allocate and initialize a task within the scheduler's task table. The caller is expected to hold the task table lock.
/// @param scheduler The task scheduler that owns the task.
/// @param task_entry The entry point of task execution.
/// @param task_args User-supplied arguments used to launch the task.
/// @param dependency The handle of the task that must complete before the task can launch.
internal_function task_handle_t
AllocateTask_NoLock
(
    WIN32_TASK_SCHEDULER  *scheduler,
    WIN32_TASK_ENTRYPOINT task_entry, 
    void                  *task_args, 
    task_handle_t         dependency
)
{
    if (scheduler->TaskCount < WIN32_TASK_SCHEDULER::MAX_LIVE_JOBS)
    {
        TASK_INDEX       &index = scheduler->Indices[scheduler->FreeListHead]; // retrieve the next free object index
        scheduler->FreeListHead = index.Next;                                  // pop the item from the free list
        index.Id                += WIN32_TASK_SCHEDULER::GENERATION_NEXT;       // assign the task a unique ID
        index.Index             = uint16_t(scheduler->TaskCount++);            // allocate the next unused slot in the packed arrays
        WIN32_TASK_WORKITEM &wi = scheduler->WorkItems[index.Index];           // grab the work item to populate
        wi.TaskEntry            = task_entry;                                  // store the user-supplied entry point
        wi.TaskArgs             = task_args;                                   // store the user-supplied argument data
        wi.TaskId               = index.Id;                                    // store the internal task ID 
        scheduler->WorkCounts  [index.Index] = 2;                              // 1 for self, 1 to keep task alive in case of race
        scheduler->Dependencies[index.Index] = dependency;                     // store the task this task waits for
        return MakeTaskHandle(index.Id, WIN32_TASK_TYPE_NON_BLOCKING);
    }
    else return INVALID_TASK_HANDLE;
}

/// @summary Invalidate a task within the scheduler's task table. The caller is expected to hold the task table lock.
/// @param scheduler The task scheduler that owns the task.
/// @param handle The handle of the task to invalidate.
internal_function void
DeleteTask_NoLock
(
    WIN32_TASK_SCHEDULER *scheduler,
    task_handle_t            handle
)
{
    uint32_t   const taskid   = GetTaskTableId(handle);
    uint16_t   const arrayid  = uint16_t(taskid & WIN32_TASK_SCHEDULER::INDEX_MASK);
    TASK_INDEX       &index   = scheduler->Indices[arrayid];
    if (index.Id  == taskid  && index.Index != WIN32_TASK_SCHEDULER::INDEX_INVALID)
    {   // swap the last task data into the slot occupied by the task being deleted.
        scheduler->WorkItems   [index.Index] = scheduler->WorkItems   [scheduler->TaskCount-1];
        scheduler->WorkCounts  [index.Index] = scheduler->WorkCounts  [scheduler->TaskCount-1];
        scheduler->Dependencies[index.Index] = scheduler->Dependencies[scheduler->TaskCount-1];
        // fixup the indirection table for the item that was just moved.
        scheduler->Indices[scheduler->WorkItems[index.Index].TaskId & WIN32_TASK_SCHEDULER::INDEX_MASK].Index = index.Index;
        scheduler->TaskCount--;
        // invalidate the handle and return the item to the free list.
        index.Index = WIN32_TASK_SCHEDULER::INDEX_INVALID;
        scheduler->Indices[scheduler->FreeListTail].Next = uint16_t(arrayid);
        scheduler->FreeListTail = uint16_t(arrayid);
    }
}

internal_function WIN32_TASK*
TaskQueuePush
(
    WIN32_TASK_QUEUE *queue, 
    WIN32_TASK const  &task
)
{
    uint32_t b   = queue->Bottom.load(std::memory_order_relaxed);
    uint32_t t   = queue->Top.load(std::memory_order_acquire);
    if ((b - t) <= WIN32_TASK_QUEUE::MASK)
    {   // the queue is not full, so copy the task data into the queue.
        WIN32_TASK *item = &queue->Task[b & WIN32_TASK_QUEUE::MASK];
        CopyMemory (item, &task, sizeof(WIN32_TASK));
        // ensure that the task data is copied before incrementing the bottom index.
        std::atomic_thread_fence(std::memory_order_release);
        // make the item visible to other threads.
        queue->Bottom.store(b+1, std::memory_order_relaxed);
        return item;
    }
    else
    {   // the queue is currently full; the item cannot be added.
        return NULL;
    }
}

internal_function int
TaskQueueTake
(
    WIN32_TASK_QUEUE *queue, 
    WIN32_TASK        &task
)
{
    int result = WIN32_TASK_QUEUE_GET_SUCCESS;
    uint32_t b = queue->Bottom.load(std::memory_order_relaxed) - 1;
    queue->Bottom.store(b, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);

    uint32_t t = queue->Top.load(std::memory_order_relaxed);
    if (t <= b)
    {   // the task queue is non-empty. copy the task data to the output location.
        CopyMemory(&task, &queue->Task[b & WIN32_TASK_QUEUE::MASK], sizeof(WIN32_TASK));
        if (t != b)
        {   // there's at least one more item in the queue; no need to race.
            return WIN32_TASK_QUEUE_GET_SUCCESS;
        }
        // this was the last item in the queue. race to claim it.
        if (queue->Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // this thread won the race and claimed the last item.
            result = WIN32_TASK_QUEUE_GET_SUCCESS;
        }
        else
        {   // this thread lost the race.
            result = WIN32_TASK_QUEUE_GET_EMPTY;
        }
        // restore the bottom marker.
        queue->Bottom.store(b+1, std::memory_order_relaxed);
        return result;
    }
    else
    {   // the queue is empty; don't return any data to the caller.
        return WIN32_TASK_QUEUE_GET_EMPTY;
    }
}

internal_function int
TaskQueueSteal
(
    WIN32_TASK_QUEUE *queue, 
    WIN32_TASK        &task
)
{
    uint32_t t = queue->Top.load(std::memory_order_acquire);
    std::atomic_thread_fence(std::memory_order_seq_cst);
    uint32_t b = queue->Bottom.load(std::memory_order_acquire);

    if (t < b)
    {   // the task queue is non-empty. copy the item data.
        CopyMemory(&task, &queue->Task[t & WIN32_TASK_QUEUE::MASK], sizeof(WIN32_TASK));
        // race with other threads to claim the item.
        if (queue->Top.compare_exchange_strong(t, t + 1, std::memory_order_seq_cst, std::memory_order_relaxed))
        {   // the calling thread won the race and claimed the item.
            return WIN32_TASK_QUEUE_GET_SUCCESS;
        }
        else
        {   // the calling thread lost the race and should try again.
            return WIN32_TASK_QUEUE_GET_ABORT;
        }
    }
    else
    {   // the queue is currently empty.
        return WIN32_TASK_QUEUE_GET_EMPTY;
    }
}

internal_function unsigned int __cdecl
TaskSchedulerWorkerThread
(
    void *args
)
{
    WIN32_TASK_THREAD_ARGS   *thread_args =(WIN32_TASK_THREAD_ARGS*) args;
    WIN32_THREAD_ARGS          *main_args = thread_args->MainThreadArgs;
    HANDLE                  go_or_halt[2] = { main_args->TerminateEvent, thread_args->StartSignal };

    // TODO(rlk): Set up arena and local task queue.

    // signal the owning thread that this worker is ready to work.
    SetEvent(thread_args->ReadySignal);

    // wait for the signal to either start looking for work, or to terminate early.
    switch (WaitForMultipleObjects(2, go_or_halt, FALSE, INFINITE))
    {
        case WAIT_OBJECT_0    : goto terminate_worker;
        case WAIT_OBJECT_0 + 1: goto launch_worker;
        default:
            {   // some kind of serious error.
            } break;
    }

launch_worker:
    ConsoleOutput("Worker thread %u launched.\n", thread_args->InternalId);

terminate_worker:
    ConsoleOutput("Worker thread %u terminated.\n", thread_args->InternalId);
    return 0;
}

/// @summary Initialize a task scheduler instance and spawn all of the worker threads. The calling thread is blocked until the scheduler is available.
/// @param scheduler The task scheduler instance to initialize and launch.
/// @param main_args The global data maintained by the main thread.
/// @return true if the scheduler has launched successfully.
internal_function bool
LaunchTaskScheduler
(
    WIN32_TASK_SCHEDULER *scheduler, 
    WIN32_THREAD_ARGS    *main_args
)
{   
    HANDLE                   launch_signal = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE                    error_signal = CreateEvent(NULL, TRUE, FALSE, NULL);
    HANDLE                 *worker_threads = NULL;
    unsigned int               *worker_ids = NULL;
    WIN32_TASK_THREAD_ARGS    *worker_args = NULL;
    size_t                    thread_count = 0;

    // TODO(rlk): config to set up memory arena(s).
    // initialize the scheduler fields to known values.
    ZeroMemory(scheduler, sizeof(WIN32_TASK_SCHEDULER));

    // initialize the task scheduler's task table.
    if (!InitializeCriticalSectionAndSpinCount(&scheduler->TaskTableLock, 0x1000))
    {   // unable to create the mutex protecting the task table.
        goto cleanup_and_fail;
    }
    ClearSchedulerTaskTable(scheduler);

    // allocate storage for per-thread data.
    thread_count   = main_args->HostCPUInfo->HardwareThreads;
    worker_args    = (WIN32_TASK_THREAD_ARGS*) malloc(thread_count * sizeof(WIN32_TASK_THREAD_ARGS));
    worker_ids     = (unsigned int          *) malloc(thread_count * sizeof(unsigned int));
    worker_threads = (HANDLE                *) malloc(thread_count * sizeof(HANDLE));
    if (worker_args == NULL || worker_ids == NULL || worker_threads == NULL)
    {   // unable to allocate the required global memory.
        goto cleanup_and_fail;
    }
    ZeroMemory(worker_threads, thread_count * sizeof(HANDLE));
    ZeroMemory(worker_ids    , thread_count * sizeof(unsigned int));

    // spawn all of the worker threads in the pool, one per hardware thread.
    // spawn and wait sequentially, which is slower to start up, but simpler.
    for (size_t i = 0; i < thread_count; ++i)
    {
        unsigned int               thread_id = 0;
        uintptr_t              thread_handle = 0;
        WIN32_TASK_THREAD_ARGS  *thread_args = &worker_args[i];
        HANDLE                  thread_ready = CreateEvent(NULL, TRUE, FALSE, NULL);

        // set up the data to be passed to the worker thread.
        thread_args->MainThreadArgs = main_args;
        thread_args->ReadySignal    = thread_ready;
        thread_args->StartSignal    = launch_signal;
        thread_args->ErrorSignal    = error_signal;
        thread_args->InternalId     =(uint32_t) i;

        // spawn the thread, and wait for it to report that it's ready.
        if ((thread_handle = _beginthreadex(NULL, 0, TaskSchedulerWorkerThread, thread_args, 0, &thread_id)) == 0)
        {   // unable to spawn the thread; terminate everybody and exit.
            SetEvent(error_signal);
            goto cleanup_and_fail;
        }

        // wait for the worker thread to report that it's ready for work.
        HANDLE  error_or_ready[2] = { error_signal, thread_ready };
        switch (WaitForMultipleObjects(2, error_or_ready, FALSE, INFINITE))
        {
            case WAIT_OBJECT_0:
                {   // the worker thread reported a fatal error.
                    // all other worker threads will exit. cleanup.
                    goto cleanup_and_fail;
                };

            case WAIT_OBJECT_0 + 1:
                {   // the worker thread reported that it is ready to go.
                    // save off the thread information within the scheduler.
                    worker_threads[i] =(HANDLE) thread_handle;
                    worker_ids[i]     = thread_id;
                } break;

            default:
                {   // the wait failed. this is a serious error. terminate all workers.
                    SetEvent(error_signal);
                    goto cleanup_and_fail;
                }
        }
    }

    // all threads are ready to receive work. update the scheduler fields.
    scheduler->ErrorSignal   = error_signal;
    scheduler->LaunchSignal  = launch_signal;
    scheduler->ThreadCount   = thread_count;
    scheduler->WorkerIds     = worker_ids;
    scheduler->WorkerThreads = worker_threads;

    // launch all of the worker threads so they can start processing work.
    SetEvent(launch_signal);
    return true;

cleanup_and_fail:
    if (worker_threads != NULL) free(worker_threads);
    if (worker_ids     != NULL) free(worker_ids);
    if (worker_args    != NULL) free(worker_args);
    if (error_signal   != NULL) CloseHandle(error_signal);
    if (launch_signal  != NULL) CloseHandle(launch_signal);
    return false;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Allocates a new task queue from a memory arena and initializes it to empty.
/// @param arena The memory arena to allocate from.
/// @return A pointer to the initialized task queue, or NULL.
public_function WIN32_TASK_QUEUE*
MakeTaskQueue
(
    MEMORY_ARENA *arena
)
{
    WIN32_TASK_QUEUE  *queue = (WIN32_TASK_QUEUE*) ArenaAllocate(arena, sizeof(WIN32_TASK_QUEUE), WIN32_TASK_QUEUE::ALIGNMENT);
    if (queue != NULL)
    {   // initialize the queue to empty.
        queue->Top.store(0, std::memory_order_relaxed);
        queue->Bottom.store(0, std::memory_order_relaxed);
    }
    return queue;
}

/// @summary Clear a task queue to empty.
/// @param queue The task queue to empty.
public_function void
ClearTaskQueue
(
    WIN32_TASK_QUEUE *queue
)
{
    queue->Top.store(0, std::memory_order_relaxed);
    queue->Bottom.store(0, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_seq_cst);
}

/// @summary Allocate and launch a new compute-oriented task scheduler.
/// @param arena The memory arena to allocate from.
/// @param main_args The global data made available by the main thread.
/// @return A pointer to the active task scheduler, or NULL.
public_function WIN32_TASK_SCHEDULER*
MakeTaskScheduler
(
    MEMORY_ARENA          *arena, 
    WIN32_THREAD_ARGS *main_args
)
{   // TODO(rlk): need to separate into allocation and launch steps.
    WIN32_TASK_SCHEDULER *scheduler = PushStruct<WIN32_TASK_SCHEDULER>(arena);
    LaunchTaskScheduler(scheduler, main_args);
    return scheduler;
}

// TODO(rlk): The main thread submits the 'root' work items. It has its own job queue in which it can place items.
// The task scheduler is given a reference to this job queue, and allows all of the worker threads to steal from it, 
// which is how they get work. Unlike some other task systems, we don't want the main thread to be doing work itself 
// as that would affect its ability to accurately launch ticks. Does that really matter, though, if the system 
// can't keep up? It needs to limit its launch ability to some number of ticks in-flight at any given time. This 
// could be done using a counting semaphore.
// 
// Actually, it would be nice to have the ability to register arbitrary work queues with the scheduler so that for 
// example async task threads could generate work items; for example, say you use the async scheduler to read a 
// JPEG file from disk into memory. When that completes, the async task would enqueue a work item within the compute
// task scheduler to decode the in-memory data.
// 
// Getting really crazy, it might be possible to make this work bi-directionally to implement a sort of yield mechanism, 
// where if a compute task needs to do something that would block, it can create an async scheduler job to represent 
// the blocking work. That sounds mighty complex, though.
//
// There's another problem we have to deal with.
// The MM job system assumes that the job set is wiped at the end of the tick. This is no good because we can't then overlap ticks.
// Alongside that, we have the problem of job references via parent (needed by FinishJob) and dependent. IMO this means that a list
// of jobs is needed somewhere to track data such as:
// 1. Reference count. This is different than OpenWorkItems, which tracks a job+children, but does not account for dependents.
//    When the reference count drops to 0, the job can be deleted from the list.
// 2. Open work items. This tracks whether or not a job is considered to be finished.
// The problem with having a global job list is that it introduces variable locking overhead into the system. It would be better if
// the deletions could all be performed at once, but that requires a scheduler tick job to run.
//
// Let's think through some things.
// - Jobs are identified via handle (32-bit integer) in the global job list.
// - Spawning a job never moves an existing job in the job list.
// - The job table is protected by a reader-writer lock.
// - The scheduler itself maintains a work item queue.
//
// add root task
//   - if scheduler decides the task is ready to run, it adds it to its local work queue; worker threads can steal from this queue.
//   - else it adds to the pending queue?
//
// root tasks have no dependencies.
// begin add task(s) locks global table. if performing a batch add, create one pseudo-task with children; return handle to pseudo-task.
// finish add task allows user to specify queue, or NULL to use main scheduler queue. worker threads would specify their own queue.
// when finishing, a task needs to know what tasks depend on it, so it can launch them.

