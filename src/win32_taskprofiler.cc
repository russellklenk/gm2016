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

/*/////////////////
//   Constants   //
/////////////////*/

/*////////////////////////////
//   Forward Declarations   //
////////////////////////////*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define bitflags used to indicate the state of a task system profiler instance.
enum PROFILER_FLAGS : uint32_t
{
    PROFILER_FLAGS_NONE      = (0UL << 0),       /// The profiler is idle and is not actively capturing events.
    PROFILER_FLAGS_CAPTURE   = (1UL << 0),       /// The profiler is actively capturing events.
};

/// @summary Defines the data associated with a context switch event. This data is extracted from the EVENT_RECORD::UserData field.
/// See https://msdn.microsoft.com/en-us/library/windows/desktop/aa364115%28v=vs.85%29.aspx
/// See https://msdn.microsoft.com/en-us/library/aa964744%28VS.85%29.aspx
struct WIN32_CONTEXT_SWITCH
{
    uint32_t                 PrevThreadId;       /// The system identifier of the thread being switched out.
    uint32_t                 CurrThreadId;       /// The system identifier of the thread being switched in.
    uint32_t                 WaitTimeMs;         /// The number of milliseconds that the CurrThreadId thread spent in a wait state.
    int8_t                   WaitReason;         /// The reason the switch-out thread is being placed into a wait state.
    int8_t                   WaitMode;           /// Whether the switched-out thread has entered a kernel-mode wait (0) or a user-mode wait (1).
    int8_t                   PrevThreadPriority; /// The priority value of the thread being switched out.
    int8_t                   CurrThreadPriority; /// The priority value of the thread being switched in.
};

/// @summary Define the data managed by the built-in profiler.
struct WIN32_TASK_PROFILER
{   static size_t const      MAX_EVENTS            = 256 * 1024;

    uint64_t                 Mask;               /// The mask value used to convert event counts into array indices.

    TRACEHANDLE              ConsumerHandle;     /// The ETW trace handle returned by OpenTrace.
    HANDLE                   ConsumerLaunch;     /// A manual-reset event used by the consumer thread to signal the start of trace capture.
    HANDLE                   ConsumerThread;     /// The HANDLE of the thread that receives context switch events.
    unsigned int             ConsumerThreadId;   /// The system identifier of the thread that receives context switch events.
    uint32_t                 ProfilerState;      /// One or more of PROFILER_FLAGS indicating the current profiler state.

    uint8_t                 *ETWBuffer;          /// A 64KB buffer used for parsing event data returned by ETW.
    uint64_t                 CSwitchCount;       /// The number of captured context switch events.
    uint64_t                *CSwitchTime;        /// An array of timestamp values (in ticks) for the context switch events.
    WIN32_CONTEXT_SWITCH    *CSwitchData;        /// An array of context switch event data.

    uint64_t                 ClockFrequency;     /// The frequency of the high-resolution timer on the system, in ticks-per-second.
    TRACEHANDLE              SessionHandle;      /// The ETW trace handle for the session.
    EVENT_TRACE_PROPERTIES  *SessionProperties;  /// The trace session properties.
    MEMORY_ARENA             MemoryArena;        /// The memory arena used for allocating all profiler memory.
    WIN32_MEMORY_ARENA       OSMemoryArena;      /// The OS memory arena used for all of the profiler memory.
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Retrieve a 32-bit unsigned integer property value from an event record.
/// @param ev The EVENT_RECORD passed to TaskProfilerRecordEvent.
/// @param info_buf The TRACE_EVENT_INFO containing event metadata.
/// @param index The zero-based index of the property to retrieve.
/// @return The integer value.
internal_function inline uint32_t
TaskProfilerGetUInt32
(
    EVENT_RECORD           *ev, 
    TRACE_EVENT_INFO *info_buf, 
    size_t               index
)
{
    PROPERTY_DATA_DESCRIPTOR dd;
    uint32_t  value =  0;
    dd.PropertyName = (ULONGLONG)((uint8_t*) info_buf + info_buf->EventPropertyInfoArray[index].NameOffset);
    dd.ArrayIndex   =  ULONG_MAX;
    dd.Reserved     =  0;
    TdhGetProperty(ev, 0, NULL, 1, &dd, (ULONG) sizeof(uint32_t), (PBYTE) &value);
    return value;
}

/// @summary Retrieve an 8-bit signed integer property value from an event record.
/// @param ev The EVENT_RECORD passed to TaskProfilerRecordEvent.
/// @param info_buf The TRACE_EVENT_INFO containing event metadata.
/// @param index The zero-based index of the property to retrieve.
/// @return The integer value.
internal_function inline int8_t
TaskProfilerGetSInt8
(
    EVENT_RECORD           *ev, 
    TRACE_EVENT_INFO *info_buf, 
    size_t               index
)
{
    PROPERTY_DATA_DESCRIPTOR dd;
    int8_t    value =  0;
    dd.PropertyName = (ULONGLONG)((uint8_t*) info_buf + info_buf->EventPropertyInfoArray[index].NameOffset);
    dd.ArrayIndex   =  ULONG_MAX;
    dd.Reserved     =  0;
    TdhGetProperty(ev, 0, NULL, 1, &dd, (ULONG) sizeof(int8_t), (PBYTE) &value);
    return value;
}

/// @summary Callback invoked for each context switch event reported by Event Tracing for Windows.
/// @param ev Data associated with the event being reported.
internal_function void WINAPI
TaskProfilerRecordEvent
(
    EVENT_RECORD *ev
)
{
    WIN32_TASK_PROFILER *profiler = (WIN32_TASK_PROFILER*) ev->UserContext;
    TRACE_EVENT_INFO    *info_buf = (TRACE_EVENT_INFO*) profiler->ETWBuffer;
    ULONG                size_buf =  64 * 1024;

    // attempt to parse out the context switch information from the EVENT_RECORD.
    if (TdhGetEventInformation(ev, 0, NULL, info_buf, &size_buf) == ERROR_SUCCESS)
    {   // this involves some ridiculous parsing of data in an opaque buffer.
        if (info_buf->EventDescriptor.Opcode == 36)
        {   // opcode 36 corresponds to a CSwitch event.
            // see https://msdn.microsoft.com/en-us/library/windows/desktop/aa964744%28v=vs.85%29.aspx
            uint64_t             ev_index   = profiler->CSwitchCount & profiler->Mask;
            WIN32_CONTEXT_SWITCH &cswitch   = profiler->CSwitchData[ev_index];
            cswitch.CurrThreadId            = TaskProfilerGetUInt32(ev, info_buf,  0); // NewThreadId
            cswitch.PrevThreadId            = TaskProfilerGetUInt32(ev, info_buf,  1); // OldThreadId
            cswitch.WaitTimeMs              = TaskProfilerGetUInt32(ev, info_buf, 10); // NewThreadWaitTime
            cswitch.WaitReason              = TaskProfilerGetSInt8 (ev, info_buf,  6); // OldThreadWaitReason
            cswitch.WaitMode                = TaskProfilerGetSInt8 (ev, info_buf,  7); // OldThreadWaitMode
            cswitch.PrevThreadPriority      = TaskProfilerGetSInt8 (ev, info_buf,  3); // OldThreadPriority
            cswitch.CurrThreadPriority      = TaskProfilerGetSInt8 (ev, info_buf,  2); // NewThreadPriority
            profiler->CSwitchTime[ev_index] =(uint64_t) ev->EventHeader.TimeStamp.QuadPart;
            profiler->CSwitchCount++;
        }
    }
}

/// @summary Implements the entry point of the thread that dispatches ETW context switch events.
/// @param argp A pointer to the WIN32_TASK_PROFILER to which events will be logged.
/// @return Zero (unused).
internal_function unsigned int __cdecl
TaskProfilerThreadMain
(
    void *argp
)
{   // wait for the go signal from the main thread.
    // this ensures that state is properly set up.
    DWORD             wait_result =  WAIT_OBJECT_0;
    WIN32_TASK_PROFILER *profiler = (WIN32_TASK_PROFILER*) argp;
    if ((wait_result = WaitForSingleObject(profiler->ConsumerLaunch, INFINITE)) != WAIT_OBJECT_0)
    {   // the wait failed for some reason, so terminate early.
        ConsoleError("ERROR (%S): Context switch consumer wait for launch failed with result %08X (%08X).\n", __FUNCTION__, wait_result, GetLastError());
        return 1;
    }

    // because real-time event capture is being used, the ProcessTrace function 
    // doesn't return until the capture session is stopped by calling CloseTrace.
    // ProcessTrace sorts events by timestamp and calls TaskProfilerRecordEvent.
    ULONG result  = ProcessTrace(&profiler->ConsumerHandle, 1, NULL, NULL);
    if   (result != ERROR_SUCCESS)
    {   // the thread is going to terminate because an error occurred.
        ConsoleError("ERROR (%S): Context switch consumer terminating with result %08X.\n", __FUNCTION__, result);
        return 1;
    }
    return 0;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Calculate the amount of memory required to create a task profiler.
/// @return The minimum number of bytes required to store the task profiler data, not including the size of the WIN32_TASK_PROFILER instance.
public_function size_t
CalculateMemoryForTaskProfiler
(
    void
)
{
    size_t size_in_bytes = 0;
    size_in_bytes += sizeof(EVENT_TRACE_PROPERTIES);
    size_in_bytes += sizeof(KERNEL_LOGGER_NAME) + 1;
    size_in_bytes += AllocationSizeForArray<uint64_t            >(WIN32_TASK_PROFILER::MAX_EVENTS);
    size_in_bytes += AllocationSizeForArray<WIN32_CONTEXT_SWITCH>(WIN32_TASK_PROFILER::MAX_EVENTS);
    size_in_bytes += 64 * 1024; // for the event parsing buffer
    return size_in_bytes;
}

/// @summary Create a new task system profiler. Call StartTaskProfileCapture to begin capturing profile events.
/// @param profiler The WIN32_TASK_PROFILER instance to initialize.
/// @return Zero if the profiler instance is successfullyer initialized, or -1 if an error occurred.
public_function int
CreateTaskProfiler
(
    WIN32_TASK_PROFILER *profiler
)
{   
    WIN32_MEMORY_ARENA *os_arena = &profiler->OSMemoryArena;
    MEMORY_ARENA          *arena = &profiler->MemoryArena;

    // zero out all of the fields of the WIN32_TASK_PROFILER structure.
    ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));

    // start by creating the profiler-local memory arena used for all allocations.
    size_t mem_required = CalculateMemoryForTaskProfiler();
    if (CreateMemoryArena(os_arena, mem_required, true, true) < 0)
    {   // the physical address space could not be reserved or committed.
        ConsoleError("ERROR (%S): Unable to create task profiler memory arena; %zu bytes needed.\n", __FUNCTION__, mem_required);
        return -1;
    }
    if (CreateArena(arena, mem_required, std::alignment_of<void*>::value, os_arena) < 0)
    {   // this should never happen - there's an implementation error.
        ConsoleError("ERROR (%S): Unable to create task profiler user memory arena.\n", __FUNCTION__);
        ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));
        return -1;
    }

    // allocate memory for the EVENT_TRACE_PROPERTIES, which must be followed by the logger name.
    size_t             props_size = sizeof(EVENT_TRACE_PROPERTIES) + sizeof(KERNEL_LOGGER_NAME);
    uint8_t              *raw_ptr = PushArray<uint8_t>(arena, props_size);
    EVENT_TRACE_PROPERTIES *props =(EVENT_TRACE_PROPERTIES*) raw_ptr;
    WCHAR                   *name =(WCHAR*)(raw_ptr + sizeof(EVENT_TRACE_PROPERTIES));
    ULONG                  result = ERROR_SUCCESS;

    // configure Event Tracing for Windows to return context switch information to us.
    ZeroMemory(raw_ptr, props_size);
    props->Wnode.BufferSize    =(ULONG) props_size;
    props->Wnode.Guid          = SystemTraceControlGuid;
    props->Wnode.Flags         = WNODE_FLAG_TRACED_GUID;
    props->Wnode.ClientContext = 1; // use QueryPerformanceCounter timestamps
    props->EnableFlags         = EVENT_TRACE_FLAG_CSWITCH;
    props->LogFileMode         = EVENT_TRACE_REAL_TIME_MODE;
    props->LoggerNameOffset    = sizeof(EVENT_TRACE_PROPERTIES);
    StringCbCopy(name, sizeof(KERNEL_LOGGER_NAME), KERNEL_LOGGER_NAME);

    // initialize the various internal event buffers.
    profiler->Mask             = WIN32_TASK_PROFILER::MAX_EVENTS - 1;
    profiler->ETWBuffer        = PushArray<uint8_t             >(arena, 64 * 1024);
    profiler->CSwitchTime      = PushArray<uint64_t            >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->CSwitchData      = PushArray<WIN32_CONTEXT_SWITCH>(arena, WIN32_TASK_PROFILER::MAX_EVENTS);

    // retrieve the frequency of the high-resolution system timer.
    // this is used to convert timestamp values from ticks into seconds.
    LARGE_INTEGER   frequency = {};
    QueryPerformanceFrequency(&frequency);
    profiler->ClockFrequency  = (uint64_t) frequency.QuadPart;

    // stop any existing session, since for SystemTraceControlGuid, only one session 
    // is allowed system-wide, and there's no guarantee that the starting process shut 
    // it down correctly. the alternative is to reboot the machine...
    ControlTrace(NULL, KERNEL_LOGGER_NAME, props, EVENT_TRACE_CONTROL_STOP);

    // start a new session to begin capturing context switch events.
    if ((result = StartTrace(&profiler->SessionHandle, KERNEL_LOGGER_NAME, props)) != ERROR_SUCCESS)
    {   // the trace session could not be started. clean up.
        ConsoleError("ERROR (%S): Unable to start ETW trace session (%08X).\n", __FUNCTION__, result);
        DeleteMemoryArena(&profiler->OSMemoryArena);
        ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));
        return -1;
    }

    // save the session properties for use in future calls to ControlTrace.
    profiler->SessionProperties = props;
    return 0;
}

/// @summary Stops any open capture sessions and releases all resources associated with a task system profiler.
/// @param profiler The profile instance to delete.
public_function void
DeleteTaskProfiler
(
    WIN32_TASK_PROFILER *profiler
)
{
    if (profiler->ConsumerHandle != NULL)
    {   // stop the current session and wait for the consumer thread to exit.
        SetEvent(profiler->ConsumerLaunch);
        CloseTrace(profiler->ConsumerHandle);
        WaitForSingleObject(profiler->ConsumerThread, INFINITE);
        CloseHandle(profiler->ConsumerLaunch);
    }
    if (profiler->SessionProperties != NULL)
    {   // stop the system-wide trace session.
        ControlTrace(NULL, KERNEL_LOGGER_NAME, profiler->SessionProperties, EVENT_TRACE_CONTROL_STOP);
        DeleteMemoryArena(&profiler->OSMemoryArena);
    }
    ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));
}

/// @summary Begin capturing task system profile events.
/// @param profiler The profiler instance for which capturing will be enabled.
/// @return Zero if event capture is enabled, or -1 if an error occurred.
public_function int
StartTaskProfileCapture
(
    WIN32_TASK_PROFILER *profiler
)
{
    if (profiler->ConsumerHandle != NULL)
    {   // profile capture is already running.
        return 0;
    }

    HANDLE   launch_handle = NULL;
    HANDLE   thread_handle = NULL;
    unsigned int thread_id = 0;
    TRACEHANDLE     handle = INVALID_PROCESSTRACE_HANDLE;

    // create a manual-reset event used to hold the consumer thread 
    // until this thread has finished initializing the state it needs.
    if ((launch_handle = CreateEvent(NULL, TRUE, FALSE, NULL)) == NULL)
    {   // no way to synchronize the launch of the consumer thread.
        ConsoleError("ERROR (%S): Unable to create consumer launch event (%08X).\n", __FUNCTION__, GetLastError());
        return -1;
    }

    // reset the internal profiler state.
    profiler->ConsumerLaunch    = launch_handle;
    profiler->Mask              = WIN32_TASK_PROFILER::MAX_EVENTS - 1;
    profiler->CSwitchCount      = 0;

    // configure real-time event capture and set the per-event callback.
    EVENT_TRACE_LOGFILE logfile = {};
    logfile.LoggerName          = KERNEL_LOGGER_NAME;
    logfile.ProcessTraceMode    = PROCESS_TRACE_MODE_REAL_TIME | PROCESS_TRACE_MODE_EVENT_RECORD | PROCESS_TRACE_MODE_RAW_TIMESTAMP;
    logfile.EventRecordCallback = TaskProfilerRecordEvent;
    logfile.Context             =(PVOID) profiler;

    // open the trace session. this begins collecting events in the buffers.
    if ((handle = OpenTrace(&logfile)) == INVALID_PROCESSTRACE_HANDLE)
    {   // unable to open the consumer session. there are several possible reasons.
        ConsoleError("ERROR (%S): Unable to open consumer session (%08X).\n", __FUNCTION__, GetLastError());
        CloseHandle(launch_handle);
        return -1;
    }

    // spawn a thread to receive the context switch events. _beginthreadex ensures the CRT is properly initialized.
    if ((thread_handle = (HANDLE)_beginthreadex(NULL, 0, TaskProfilerThreadMain, profiler, 0, &thread_id)) == NULL)
    {   // unable to spawn the worker thread. let the caller decide if they want to terminate everybody.
        ConsoleError("ERROR (%S): Consumer thread creation failed (errno = %d).\n", __FUNCTION__, errno);
        profiler->ConsumerHandle = NULL;
        CloseTrace(handle);
        return -1;
    }

    // save the consumer thread information and then launch the consumer thread.
    profiler->ConsumerHandle   = handle;
    profiler->ConsumerThread   = thread_handle;
    profiler->ConsumerThreadId = thread_id;
    profiler->ProfilerState    = PROFILER_FLAGS_CAPTURE;
    SetEvent(launch_handle);
    return 0;
}

/// @summary Stop capturing task system profiling events. This may block the calling thread for several seconds until buffered events are captured.
/// @param profiler The profiler instance to update.
public_function void
StopTaskProfileCapture
(
    WIN32_TASK_PROFILER *profiler
)
{
    if (profiler->ConsumerHandle != NULL)
    {
        SetEvent(profiler->ConsumerLaunch);
        CloseTrace(profiler->ConsumerHandle);
        WaitForSingleObject(profiler->ConsumerThread, INFINITE);
        CloseHandle(profiler->ConsumerLaunch);
        profiler->ConsumerLaunch   = NULL;
        profiler->ConsumerHandle   = NULL;
        profiler->ConsumerThread   = NULL;
        profiler->ConsumerThreadId = 0;
        profiler->ProfilerState    = PROFILER_FLAGS_NONE;
    }
}

