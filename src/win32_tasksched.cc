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

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Use a "unique" 32-bit integer to identify a task.
typedef uint32_t task_id_t;

/// @summary Define the signature of the callback function invoked to execute a task.
typedef int (*WIN32_TASK_ENTRYPOINT)(struct TASK_ARGS *);

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

/// @summary Define the user-supplied data associated with a work item.
struct WIN32_TASK_WORKITEM
{
    WIN32_TASK_ENTRYPOINT TaskEntry;      /// The task entry point.
    void                 *TaskArgs;       /// Task-specific data associated with the work item.
};

/// @summary Define the internal data associated with a task.
struct WIN32_TASK
{
    task_id_t             TaskId;         /// The unique identifier of the task.
    task_id_t             ParentId;       /// The unique identifier of the parent task.
    task_id_t             DependencyId;   /// The unique identifier of the task this task depends on.
    int32_t               OpenWorkItems;  /// Counter to track task and child task completion.
    WIN32_TASK_WORKITEM   WorkItem;       /// User-supplied data associated with the task.
};

/// @summary Define the data associated with a task scheduler instance.
struct WIN32_TASK_SCHEDULER
{
    HANDLE                ErrorSignal;    /// Manual reset event used by workers to indicate a fatal error.
    HANDLE                LaunchSignal;   /// Manual reset event used to launch all worker threads.
    size_t                ThreadCount;    /// The number of worker threads in the pool.
    unsigned int         *WorkerIds;      /// The OS identifier for each worker thread.
    HANDLE               *WorkerThreads;  /// The handle of each worker thread in the pool.
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

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Initialize a task scheduler instance and spawn all of the worker threads. The calling thread is blocked until the scheduler is available.
public_function bool
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

