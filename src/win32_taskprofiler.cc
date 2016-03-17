/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement a low-overhead profiler for the task scheduler. The 
/// profiler uses Event Tracing for Windows to capture context switch events 
/// from the kernel, and correlates those with user-level events from the task
/// scheduler. This allows the visual presentation of which tasks run on which
/// worker threads, how long they took, how long they spent waiting, etc.
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
/// @summary Define the possible states of a task profiler instance.
enum PROFILER_STATE : uint32_t
{
    PROFILER_STATE_IDLE      = 0,                /// The profiler is idle and is not actively capturing events.
    PROFILER_STATE_CAPTURE   = 1,                /// The profiler is actively capturing events.
    PROFILER_STATE_SAVE      = 2,                /// The profiler is writing the contents of its buffers to disk.
    PROFILER_STATE_LOAD      = 3,                /// The profiler is loading its buffers from a previous session.
};

/// @summary Defines the data associated with a TASK_SOURCE definition. The Name field must point to a string literal or a string
/// that will remain valid until the end of the trace; the string is not copied into the source definition.
struct WIN32_TASK_SOURCE_DATA
{
    char const              *Name;               /// Pointer to a string literal specifying a user-friendly name for the TASK_SOURCE.
    uint32_t                 ThreadId;           /// The operating system identifier of the thread that owns the TASK_SOURCE.
    uint32_t                 SourceIndex;        /// The zero-based index of the TASK_SOURCE within the scheduler.
};

/// @summary Define the serialized format of the TASK_SOURCE definiton data.
#pragma pack(push, 1)
struct WIN32_TASK_SOURCE_DATA_ON_DISK
{
    uint64_t                 NameOffset;         /// The byte offset of the source name within the string table.
    uint32_t                 ThreadId;           /// The operating system identifier of the thread that owns the TASK_SOURCE.
    uint32_t                 SourceIndex;        /// The zero-based index of the TASK_SOURCE within the scheduler.
};
#pragma pack(pop)

/// @summary Defines the data associated with a context switch event. This data is extracted from the EVENT_RECORD::UserData field.
/// See https://msdn.microsoft.com/en-us/library/windows/desktop/aa364115%28v=vs.85%29.aspx
/// See https://msdn.microsoft.com/en-us/library/aa964744%28VS.85%29.aspx
#pragma pack(push, 1)
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
#pragma pack(pop)

/// @summary Defines the data used to look up a task definition. Since task IDs can be reused, the search is performed backwards
/// in time from the timestamp of a given event (such as a task launch event.)
#pragma pack(push, 1)
struct WIN32_TASK_AND_TIME
{
    uint64_t                 Timestamp;          /// The timestamp value (in ticks) at which the task was defined.
    uint32_t                 TaskId;             /// The task identifier.
    uint32_t                 Reserved;           /// Reserved for future use.
};
#pragma pack(pop)

/// @summary Defines the data associated with a task definition. The Name field must point to a string literal or a string that 
/// will remain valid until the end of the trace; the string is not copied into the task definition.
struct WIN32_TASK_DEFINITION
{   static size_t const      MAX_DEPENDENCIES      = 3;
    char const              *Name;               /// A pointer to a string literal specifying the task name.
    uint32_t                 SourceIndex;        /// The zero-based index of the TASK_SOURCE that defined the task.
    uint32_t                 ParentTaskId;       /// The identifier of the parent task, or INVALID_TASK_ID.
    uint32_t                 DependencyCount;    /// The number of dependencies that must be satisfied before the task can launch.
    uint32_t                 DependencyIds[3];   /// The task_id_t of the first three dependencies. Additional dependencies are not stored.
};

/// @summary Define the serialized format of the task definiton data.
#pragma pack(push, 1)
struct WIN32_TASK_DEFINITION_ON_DISK
{   static size_t const      MAX_DEPENDENCIES      = 3;
    uint64_t                 NameOffset;         /// The byte offset of the task name within the string table.
    uint32_t                 SourceIndex;        /// The zero-based index of the TASK_SOURCE that defined the task.
    uint32_t                 ParentTaskId;       /// The identifier of the parent task, or INVALID_TASK_ID.
    uint32_t                 DependencyCount;    /// The number of dependencies that must be satisfied before the task can launch.
    uint32_t                 DependencyIds[3];   /// The task_id_t of the first three dependencies. Additional dependencies are not stored.
};
#pragma pack(pop)

/// @summary Define the data associated with a task's transition to the ready-to-run state.
#pragma pack(push, 1)
struct WIN32_TASK_READY_DATA
{
    uint32_t                 TaskId;             /// The task identifier.
};
#pragma pack(pop)

/// @summary Define the data associated with a task launch event. This indicates the beginning of the execution of a task on a worker thread.
#pragma pack(push, 1)
struct WIN32_TASK_LAUNCH_DATA
{
    uint32_t                 TaskId;             /// The task identifier.
    uint32_t                 WorkerThreadId;     /// The operating system identifier of the thread that executed the task.
};
#pragma pack(pop)

/// @summary Define the data associated with a task's completion.
#pragma pack(push, 1)
struct WIN32_TASK_FINISH_DATA
{
    uint32_t                 TaskId;             /// The task identifier.
};
#pragma pack(pop)

/// @summary Define the data managed by the built-in profiler.
struct WIN32_TASK_PROFILER
{   static size_t const      MAX_EVENTS            = 256 * 1024;  /// The maximum number of captured events. Must be a power-of-two.
    static size_t const      MAX_TASK_SOURCES      = 4096;        /// The maximum number of task sources. Must be a power-of-two.
    static size_t const      MAX_WORKER_THREADS    = 4096;        /// The maximum number of worker threads per-pool. Must be a power-of-two.
    static size_t const      MAX_TICKS             = 1024;        /// The maximum number of tick markers. Must be a power-of-two.
    static size_t const      ETW_BUFFER_SIZE       = 64  * 1024;  /// The size of an intermediate buffer used to parse ETW events, in bytes.

    uint32_t                 TickMask;           /// The mask value used to convert tick counts into array indices.
    uint32_t                 EventMask;          /// The mask value used to convert event counts into array indices.
    uint32_t                 SourceMask;         /// The mask value used to convert source counts into array indices.
    uint32_t                 ThreadMask;         /// The mask value used to convert thread counts into array indices.

    TRACEHANDLE              ConsumerHandle;     /// The ETW trace handle returned by OpenTrace.
    HANDLE                   ConsumerLaunch;     /// A manual-reset event used by the consumer thread to signal the start of trace capture.
    HANDLE                   ConsumerThread;     /// The HANDLE of the thread that receives context switch events.
    unsigned int             ConsumerThreadId;   /// The system identifier of the thread that receives context switch events.
    std::atomic<uint32_t>    ProfilerState;      /// One of PROFILER_STATE indicating the current profiler state.

    uint32_t                 CPWorkerCount;      /// The number of worker threads defined in the compute pool.
    uint32_t                *CPWorkerThreadId;   /// The OS thread identifier of each worker thread in the compute pool.

    uint32_t                 GPWorkerCount;      /// The number of worker threads defined in the general pool.
    uint32_t                *GPWorkerThreadId;   /// The OS thread identifier of each worker thread in the general pool.

    std::atomic<uint32_t>    TaskSourceCount;    /// The number of TASK_SOURCE objects allocated for use.
    WIN32_TASK_SOURCE_DATA  *TaskSourceData;     /// The name and OS thread identifier of each allocated TASK_SOURCE.

    uint8_t                 *ETWBuffer;          /// A 64KB buffer used for parsing event data returned by ETW.
    uint32_t                 CSwitchCount;       /// The number of captured context switch events.
    uint64_t                *CSwitchTime;        /// An array of timestamp values (in ticks) for the context switch events.
    WIN32_CONTEXT_SWITCH    *CSwitchData;        /// An array of context switch event data.

    uint32_t                 TickCount;          /// The number of tick launch events passed to the profiler.
    uint64_t                *TickLaunchTime;     /// An array of timestamps (in ticks) at which each logical tick was launched.

    std::atomic<uint32_t>    TaskDefCount;       /// The number of task definitions passed to the profiler.
    WIN32_TASK_AND_TIME     *TaskDefTime;        /// An array of task IDs and timestamps for task definitions.
    WIN32_TASK_DEFINITION   *TaskDefData;        /// An array of task definition data.

    std::atomic<uint32_t>    TaskRTRCount;       /// The number of ready-to-run transitions passed to the profiler.
    uint64_t                *TaskRTRTime;        /// An array of timestamps (in ticks) at which each task was made ready-to-run.
    WIN32_TASK_READY_DATA   *TaskRTRData;        /// An array of task IDs corresponding to the ready-to-run event timestamps.

    std::atomic<uint32_t>    TaskLaunchCount;    /// The number of task launch events passed to the profiler.
    uint64_t                *TaskLaunchTime;     /// An array of timestamps (in ticks) at which each task launch occurred.
    WIN32_TASK_LAUNCH_DATA  *TaskLaunchData;     /// An array of task ID and worker thread ID for each task launch event.

    std::atomic<uint32_t>    TaskFinishCount;    /// The number of task finish events passed to the profiler.
    uint64_t                *TaskFinishTime;     /// An array of timestamps (in ticks) at which each task completion occurred.
    WIN32_TASK_FINISH_DATA  *TaskFinishData;     /// An array of data associated with each task finish event.

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
#if ENABLE_TASK_PROFILER
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
    TdhGetProperty_Func(ev, 0, NULL, 1, &dd, (ULONG) sizeof(uint32_t), (PBYTE) &value);
    return value;
}
#endif

/// @summary Retrieve an 8-bit signed integer property value from an event record.
/// @param ev The EVENT_RECORD passed to TaskProfilerRecordEvent.
/// @param info_buf The TRACE_EVENT_INFO containing event metadata.
/// @param index The zero-based index of the property to retrieve.
/// @return The integer value.
#if ENABLE_TASK_PROFILER
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
    TdhGetProperty_Func(ev, 0, NULL, 1, &dd, (ULONG) sizeof(int8_t), (PBYTE) &value);
    return value;
}
#endif

/// @summary Callback invoked for each context switch event reported by Event Tracing for Windows.
/// @param ev Data associated with the event being reported.
#if ENABLE_TASK_PROFILER
internal_function void WINAPI
TaskProfilerRecordEvent
(
    EVENT_RECORD *ev
)
{
    WIN32_TASK_PROFILER *profiler = (WIN32_TASK_PROFILER*) ev->UserContext;
    TRACE_EVENT_INFO    *info_buf = (TRACE_EVENT_INFO*) profiler->ETWBuffer;
    ULONG                size_buf = (ULONG) WIN32_TASK_PROFILER::ETW_BUFFER_SIZE;

    // attempt to parse out the context switch information from the EVENT_RECORD.
    if (TdhGetEventInformation_Func(ev, 0, NULL, info_buf, &size_buf) == ERROR_SUCCESS)
    {   // this involves some ridiculous parsing of data in an opaque buffer.
        if (info_buf->EventDescriptor.Opcode == 36)
        {   // opcode 36 corresponds to a CSwitch event.
            // see https://msdn.microsoft.com/en-us/library/windows/desktop/aa964744%28v=vs.85%29.aspx
            // all context switch event handling occurs on the same thread.
            uint64_t             ev_index   = profiler->CSwitchCount & profiler->EventMask;
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
#endif

/// @summary Implements the entry point of the thread that dispatches ETW context switch events.
/// @param argp A pointer to the WIN32_TASK_PROFILER to which events will be logged.
/// @return Zero (unused).
#if ENABLE_TASK_PROFILER
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
#endif

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
    size_in_bytes += sizeof(EVENT_TRACE_PROPERTIES);             // SessionProperties
    size_in_bytes += sizeof(KERNEL_LOGGER_NAME);                 // LogFileName
    size_in_bytes += WIN32_TASK_PROFILER::ETW_BUFFER_SIZE;       // ETWBuffer
    size_in_bytes += AllocationSizeForArray<uint32_t              >(WIN32_TASK_PROFILER::MAX_WORKER_THREADS); // CPWorkerThreadId
    size_in_bytes += AllocationSizeForArray<uint32_t              >(WIN32_TASK_PROFILER::MAX_WORKER_THREADS); // GPWorkerThreadId
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_SOURCE_DATA>(WIN32_TASK_PROFILER::MAX_TASK_SOURCES);   // TaskSourceData
    size_in_bytes += AllocationSizeForArray<uint64_t              >(WIN32_TASK_PROFILER::MAX_EVENTS);         // CSwitchTime
    size_in_bytes += AllocationSizeForArray<WIN32_CONTEXT_SWITCH  >(WIN32_TASK_PROFILER::MAX_EVENTS);         // CSwitchData
    size_in_bytes += AllocationSizeForArray<uint64_t              >(WIN32_TASK_PROFILER::MAX_TICKS);          // TickLaunchTime
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_AND_TIME   >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskDefTime
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_DEFINITION >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskDefData
    size_in_bytes += AllocationSizeForArray<uint64_t              >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskRTRTime
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_READY_DATA >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskRTRData
    size_in_bytes += AllocationSizeForArray<uint64_t              >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskLaunchTime
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_LAUNCH_DATA>(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskLaunchData
    size_in_bytes += AllocationSizeForArray<uint64_t              >(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskFinishTime
    size_in_bytes += AllocationSizeForArray<WIN32_TASK_FINISH_DATA>(WIN32_TASK_PROFILER::MAX_EVENTS);         // TaskFinishData
    return size_in_bytes;
}

/// @summary Create a new task system profiler. Call StartTaskProfileCapture to begin capturing profile events.
/// @param profiler The WIN32_TASK_PROFILER instance to initialize.
/// @return Zero if the profiler instance is successfully initialized, or -1 if an error occurred.
#if ENABLE_TASK_PROFILER
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

    // initialize the bitmask values used to convert a count into an array index.
    profiler->TickMask      = WIN32_TASK_PROFILER::MAX_TICKS          - 1;
    profiler->EventMask     = WIN32_TASK_PROFILER::MAX_EVENTS         - 1;
    profiler->SourceMask    = WIN32_TASK_PROFILER::MAX_TASK_SOURCES   - 1;
    profiler->ThreadMask    = WIN32_TASK_PROFILER::MAX_WORKER_THREADS - 1;
    profiler->ProfilerState = PROFILER_STATE_IDLE;

    // initialize the various internal event buffers.
    profiler->ETWBuffer          = PushArray<uint8_t               >(arena, WIN32_TASK_PROFILER::ETW_BUFFER_SIZE);
    profiler->CPWorkerThreadId   = PushArray<uint32_t              >(arena, WIN32_TASK_PROFILER::MAX_WORKER_THREADS);
    profiler->GPWorkerThreadId   = PushArray<uint32_t              >(arena, WIN32_TASK_PROFILER::MAX_WORKER_THREADS);
    profiler->TaskSourceData     = PushArray<WIN32_TASK_SOURCE_DATA>(arena, WIN32_TASK_PROFILER::MAX_TASK_SOURCES);
    profiler->CSwitchTime        = PushArray<uint64_t              >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->CSwitchData        = PushArray<WIN32_CONTEXT_SWITCH  >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TickLaunchTime     = PushArray<uint64_t              >(arena, WIN32_TASK_PROFILER::MAX_TICKS);
    profiler->TaskDefTime        = PushArray<WIN32_TASK_AND_TIME   >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskDefData        = PushArray<WIN32_TASK_DEFINITION >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskRTRTime        = PushArray<uint64_t              >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskRTRData        = PushArray<WIN32_TASK_READY_DATA >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskLaunchTime     = PushArray<uint64_t              >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskLaunchData     = PushArray<WIN32_TASK_LAUNCH_DATA>(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskFinishTime     = PushArray<uint64_t              >(arena, WIN32_TASK_PROFILER::MAX_EVENTS);
    profiler->TaskFinishData     = PushArray<WIN32_TASK_FINISH_DATA>(arena, WIN32_TASK_PROFILER::MAX_EVENTS);

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
#else
public_function int
CreateTaskProfiler
(
    WIN32_TASK_PROFILER *profiler
)
{   // profiling is disabled. just zero out the data.
    ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));
    return 0;
}
#endif /* ENABLE_TASK_PROFILER */

/// @summary Stops any open capture sessions and releases all resources associated with a task system profiler.
/// @param profiler The profile instance to delete.
#if ENABLE_TASK_PROFILER
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
#else
public_function void
DeleteTaskProfiler
(
    WIN32_TASK_PROFILER *profiler
)
{   // profiling is disabled. there are no resources to clean up.
    ZeroMemory(profiler, sizeof(WIN32_TASK_PROFILER));
}
#endif

/// @summary Begin capturing task system profile events.
/// @param profiler The profiler instance for which capturing will be enabled.
/// @return Zero if event capture is enabled, or -1 if an error occurred.
#if ENABLE_TASK_PROFILER
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

    // reset the internal profiler state, not including the state that 
    // corresponds to global resources such as worker threads and task sources.
    profiler->ConsumerLaunch    = launch_handle;
    profiler->CSwitchCount      = 0;
    profiler->TaskDefCount      = 0;
    profiler->TaskRTRCount      = 0;
    profiler->TaskLaunchCount   = 0;
    profiler->TaskFinishCount   = 0;

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
    profiler->ConsumerHandle    = handle;
    profiler->ConsumerThread    = thread_handle;
    profiler->ConsumerThreadId  = thread_id;
    profiler->ProfilerState.store(PROFILER_STATE_CAPTURE, std::memory_order_seq_cst);
    SetEvent(launch_handle);
    return 0;
}
#else
public_function int
StartTaskProfileCapture
(
    WIN32_TASK_PROFILER *profiler
)
{   // profiling is disabled. return success.
    UNUSED_ARG(profiler);
    return 0;
}
#endif

/// @summary Stop capturing task system profiling events. This may block the calling thread for several seconds until buffered events are captured.
/// @param profiler The profiler instance to update.
#if ENABLE_TASK_PROFILER
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
        profiler->ProfilerState.store(PROFILER_STATE_IDLE, std::memory_order_seq_cst);
    }
}
#else
public_function void
StopTaskProfileCapture
(
    WIN32_TASK_PROFILER *profiler
)
{   // profiling is disabled. nothing to do.
    UNUSED_ARG(profiler);
}
#endif

/// @summary Unregisters all currently known global state associated with a profiler instance.
/// Worker threads and task sources will need to be re-registered, and the tick counter is reset to zero.
/// This function should be called only when no other threads are accessing the profiler, before calling StartTaskProfileCapture.
/// @param profiler The profiler to reset.
#if ENABLE_TASK_PROFILER
public_function void
ResetGlobalProfilerState
(
    WIN32_TASK_PROFILER *profiler
)
{
    profiler->CPWorkerCount   = 0;
    profiler->GPWorkerCount   = 0;
    profiler->TaskSourceCount = 0;
    profiler->TickCount       = 0;
}
#else
#define ResetGlobalProfilerState(profiler) /* profiling disabled */
#endif

/// @summary Registers the OS thread identifier of a thread in the compute pool. This function is not thread-safe and should be called during scheduler set up.
/// @param profiler The profiler associated with the scheduler that created the worker thread.
/// @param thread_id The OS thread identifier of the worker thread.
#if ENABLE_TASK_PROFILER
public_function void
RegisterComputeWorkerThread
(
    WIN32_TASK_PROFILER *profiler, 
    uint32_t            thread_id
)
{
    uint32_t slot = profiler->CPWorkerCount & profiler->ThreadMask;
    profiler->CPWorkerThreadId[slot] = thread_id;
    profiler->CPWorkerCount++;
}
#else
#define RegisterComputeWorkerThread(profiler, thread_id) /* profiling disabled */
#endif

/// @summary Registers the OS thread identifier of a thread in the general pool. This function is not thread-safe and should be called during scheduler set up.
/// @param profiler The profiler associated with the scheduler that created the worker thread.
/// @param thread_id The OS thread identifier of the worker thread.
#if ENABLE_TASK_PROFILER
public_function void
RegisterGeneralWorkerThread
(
    WIN32_TASK_PROFILER *profiler,
    uint32_t            thread_id
)
{
    uint32_t slot = profiler->GPWorkerCount & profiler->ThreadMask;
    profiler->GPWorkerThreadId[slot] = thread_id;
    profiler->GPWorkerCount++;
}
#else
#define RegisterGeneralWorkerThread(profiler, thread_id) /* profiling disabled */
#endif

/// @summary Registers information about a TASK_SOURCE with the profiler. This function is safe for concurrent access by multiple threads.
/// @param profiler The profiler associated with the scheduler attached to the TASK_SOURCE.
/// @param source_name A NULL-terminated string specifying a friendly name for the TASK_SOURCE. This string must be a literal or otherwise live for the lifetime of the profiler; it is not copied locally.
/// @param thread_id The OS thread identifier of the thread that owns the TASK_SOURCE. This may be obtained using the SelfThreadId() function if calling from the thread that owns the TASK_SOURCE.
/// @param source_index The zero-based index of the TASK_SOURCE within the scheduler. This value can be obtained from TASK_SOURCE::SourceIndex.
#if ENABLE_TASK_PROFILER
public_function void
RegisterTaskSource
(
    WIN32_TASK_PROFILER    *profiler, 
    char const          *source_name, 
    uint32_t               thread_id,
    uint32_t            source_index
)
{
    uint32_t slot = profiler->TaskSourceCount.fetch_add(1, std::memory_order_seq_cst) & profiler->SourceMask;
    profiler->TaskSourceData[slot].Name = source_name;
    profiler->TaskSourceData[slot].ThreadId = thread_id;
    profiler->TaskSourceData[slot].SourceIndex = source_index;
}
#else
#define RegisterTaskSource(profiler, source_name, thread_id, source_index) /* profiling disabled */
#endif

/// @summary Log a marker indicating the start of a tick. This function should be called from the tick launch thread only.
/// @param profiler The profiler to receive the event.
#if ENABLE_TASK_PROFILER
public_function void
MarkTickLaunch
(
    WIN32_TASK_PROFILER *profiler
)
{
    uint32_t slot = profiler->TickCount & profiler->TickMask;
    profiler->TickLaunchTime[slot] = TimestampInTicks();
    profiler->TickCount++;
}
#else
#define MarkTickLaunch(profiler) /* profiling disabled */
#endif

/// @summary Log a marker indicating when a given task was defined. This function is safe for concurrent access from multiple threads.
/// @param profiler The profiler to receive the event.
/// @param task_name A NULL-terminated string specifying a friendly name for the task. This string must be a literal or otherwise live for the lifetime of the profiler; it is not copied locally.
/// @param task_id The task identifier generated for the task by the scheduler.
/// @param source_index The zero-based index of the TASK_SOURCE that is defining the task. This value may be obtained from TASK_SOURCE::SourceIndex.
/// @param parent_id The task identifier for the parent task (if this new task is a child task) or INVALID_TASK_ID.
/// @param dependencies The list of task identifiers for any tasks that must complete before this new task becomes ready-to-run, or NULL.
/// @param dependency_count The number of task identifiers in the dependency list.
#if ENABLE_TASK_PROFILER
public_function void
MarkTaskDefinition
(
    WIN32_TASK_PROFILER        *profiler, 
    char const                *task_name,
    uint32_t                     task_id, 
    uint32_t                source_index, 
    uint32_t                   parent_id, 
    uint32_t const         *dependencies,
    size_t              dependency_count
)
{
    uint32_t slot = profiler->TaskDefCount.fetch_add(1, std::memory_order_seq_cst) & profiler->EventMask;
    profiler->TaskDefTime[slot].Timestamp       = TimestampInTicks();
    profiler->TaskDefTime[slot].TaskId          = task_id;
    profiler->TaskDefTime[slot].Reserved        = 0;
    profiler->TaskDefData[slot].Name            = task_name;
    profiler->TaskDefData[slot].SourceIndex     = source_index;
    profiler->TaskDefData[slot].ParentTaskId    = parent_id;
    profiler->TaskDefData[slot].DependencyCount = (uint32_t) dependency_count;
    uint32_t n = dependency_count > WIN32_TASK_DEFINITION::MAX_DEPENDENCIES ? 
                                    WIN32_TASK_DEFINITION::MAX_DEPENDENCIES : 
                                   (uint32_t) dependency_count;
    for (uint32_t i = 0; i < n; ++i)
    {
        profiler->TaskDefData[slot].DependencyIds[i] = dependencies[i];
    }
}
#else
#define MarkTaskDefinition(profiler, task_name, task_id, source_index, parent_id, dependencies, dependency_count) /* profiling disabled */
#endif

/// @param Log a marker indicating when a task becomes ready-to-run. This function is safe for concurrent access from multiple threads.
/// @param profiler The profiler to receive the event.
/// @param task_id The identifier of the task that has become ready-to-run.
#if ENABLE_TASK_PROFILER
public_function void
MarkTaskReadyToRun
(
    WIN32_TASK_PROFILER *profiler,
    uint32_t              task_id
)
{
    uint32_t slot = profiler->TaskRTRCount.fetch_add(1, std::memory_order_seq_cst) & profiler->EventMask;
    profiler->TaskRTRTime[slot] = TimestampInTicks();
    profiler->TaskRTRData[slot].TaskId = task_id;
}
#else
#define MarkTaskReadyToRun(profiler, task_id) /* profiling disabled */
#endif

/// @summary Log a marker indicating when a ready-to-run task starts execution on a worker thread. This function is safe for concurrent access from multiple threads.
/// @param profiler The profiler to receive the event.
/// @param task_id The identifier of the ready-to-run task being executed.
/// @param thread_id The OS thread identifier of the worker thread executing the task.
#if ENABLE_TASK_PROFILER
public_function void
MarkTaskLaunch
(
    WIN32_TASK_PROFILER *profiler, 
    uint32_t              task_id, 
    uint32_t            thread_id
)
{
    uint32_t slot = profiler->TaskLaunchCount.fetch_add(1, std::memory_order_seq_cst) & profiler->EventMask;
    profiler->TaskLaunchTime[slot] = TimestampInTicks();
    profiler->TaskLaunchData[slot].TaskId = task_id;
    profiler->TaskLaunchData[slot].WorkerThreadId = thread_id;
}
#else
#define MarkTaskLaunch(profiler, task_id, thread_id) /* profiling disabled */
#endif

/// @summary Log a marker indicating that a task has finished executing on a worker thread. This function is safe for concurrent access from multiple threads.
/// @param profiler The profiler to receive the event.
/// @param task_id The identifier of the task that finished executing.
#if ENABLE_TASK_PROFILER
public_function void
MarkTaskFinish
(
    WIN32_TASK_PROFILER *profiler, 
    uint32_t              task_id
)
{
    uint32_t slot = profiler->TaskFinishCount.fetch_add(1, std::memory_order_seq_cst) & profiler->EventMask;
    profiler->TaskFinishTime[slot] = TimestampInTicks();
    profiler->TaskFinishData[slot].TaskId = task_id;
}
#else
#define MarkTaskFinish(profiler, task_id) /* profiling disabled */
#endif

