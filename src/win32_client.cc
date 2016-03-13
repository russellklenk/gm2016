/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the client application. Initializes 
/// the runtime and sets up the execution environment for the explicit 
/// background threads. User input is handled on the main thread.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////////
//   Preprocessor   //
////////////////////*/
/// @summary Define ENABLE_TASK_PROFILER as 1 to include the task scheduler profiler.
/// Enabling the task scheduler profiler pulls in several additional dependencies.
#ifndef ENABLE_TASK_PROFILER
#define ENABLE_TASK_PROFILER 0
#endif

/// @summary If the task profiler is enabled, INITGUID needs to be #defined so that SystemTraceControlGuid is usable.
#if ENABLE_TASK_PROFILER
#define INITGUID
#endif

/// @summary Define some useful macros for specifying common resource sizes.
/// Define these before including anything so that they are available everywhere.
#define Kilobytes(x)        (size_t((x)) * size_t(1024))
#define Megabytes(x)        (size_t((x)) * size_t(1024) * size_t(1024))
#define Gigabytes(x)        (size_t((x)) * size_t(1024) * size_t(1024) * size_t(1024))

/*////////////////
//   Includes   //
////////////////*/
#include <iostream>
#include <atomic>

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>
#include <inttypes.h>

#include <process.h>
#include <conio.h>
#include <fcntl.h>
#include <io.h>

#include <tchar.h>
#include <Windows.h>
#include <Shellapi.h>
#include <XInput.h>

#if ENABLE_TASK_PROFILER
#include <strsafe.h>
#include <wmistr.h>
#include <evntrace.h>
#include <evntcons.h>
#include <tdh.h>
#endif

#include <vulkan/vulkan.h>

#include "platform_config.h"
#include "compiler_config.h"
#include "win32_platform.h"

#include "win32_debug.cc"
#include "win32_runtime.cc"
#include "win32_memarena.cc"
#include "win32_timestamp.cc"
#include "win32_scheduler.cc"

#if ENABLE_TASK_PROFILER
#include "win32_taskprofiler.cc"
#endif

#include "win32_display.cc"
#include "win32_input.cc"
#include "win32_random.cc"
#include "win32_parse.cc"

/*//////////////////
//   Data Types   //
//////////////////*/
struct LAUNCH_TASK_DATA
{
    uint8_t *Array;
    size_t   Count;
    size_t   Divisor;
};

struct TEST_TASK_DATA
{
    uint8_t *Array;
    size_t   Index;
    size_t   Count;
};

struct FENCE_TASK_DATA
{
    uint8_t *Array;
    size_t   Count;
    HANDLE   Signal;
};

struct ASYNC_TASK_DATA
{
    HANDLE   Signal;
};

/*///////////////
//   Globals   //
///////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
internal_function void
TestTaskMain
(
    task_id_t                  task_id,
    TASK_SOURCE         *thread_source, 
    TASK_DATA               *task_data, 
    MEMORY_ARENA         *thread_arena, 
    WIN32_THREAD_ARGS       *main_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{
    UNUSED_ARG(task_id);
    UNUSED_ARG(thread_source);
    UNUSED_ARG(thread_arena);
    UNUSED_ARG(main_args);
    UNUSED_ARG(scheduler);

    TEST_TASK_DATA *args = (TEST_TASK_DATA*) task_data->Data;
    for (size_t i = args->Index, e = args->Index + args->Count; i < e; ++i)
    {
        args->Array[i] = 1;
    }
    UNUSED_ARG(task_data);
}

internal_function void
LaunchTaskMain
(
    task_id_t                  task_id,
    TASK_SOURCE         *thread_source, 
    TASK_DATA               *task_data, 
    MEMORY_ARENA         *thread_arena, 
    WIN32_THREAD_ARGS       *main_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{
    UNUSED_ARG(thread_arena);
    UNUSED_ARG(main_args);
    UNUSED_ARG(scheduler);
    
    TASK_BATCH           b = {};
    LAUNCH_TASK_DATA *args = (LAUNCH_TASK_DATA*) task_data->Data;
    ZeroMemory(args->Array , args->Count * sizeof(uint8_t));
    NewTaskBatch(&b, thread_source);
    for (size_t i = 0; i < args->Count; /* empty */)
    {   // each child task will set up to 64 consecutive values to 1.
        // this should prevent cache contention between tasks.
        size_t         n = (args->Count- i) >= args->Divisor ? args->Divisor : (args->Count - i);
        TEST_TASK_DATA a = {args->Array, i, n};
        task_id_t  child =  NewComputeTask(&b, TestTaskMain, &a, task_id, TASK_SIZE_SMALL);
        UNUSED_LOCAL(child);
        i += n;
    }
}

internal_function void
FenceTaskMain
(
    task_id_t                  task_id,
    TASK_SOURCE         *thread_source, 
    TASK_DATA               *task_data, 
    MEMORY_ARENA         *thread_arena, 
    WIN32_THREAD_ARGS       *main_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{
    UNUSED_ARG(task_id);
    UNUSED_ARG(thread_source);
    UNUSED_ARG(thread_arena);
    UNUSED_ARG(main_args);
    UNUSED_ARG(scheduler);
    
    FENCE_TASK_DATA *args = (FENCE_TASK_DATA*) task_data->Data;
    bool               ok =  true;
    for (size_t i = 0,  n =  args->Count; i < n; ++i)
    {
        if (args->Array[i] != 1)
            ok = false;
    }
    //if (ok) ConsoleOutput("All tasks completed successfully!\n");
    //else ConsoleOutput("One or more tasks failed.\n");
    SetEvent(args->Signal);
}

internal_function void
GeneralTaskMain
(
    task_id_t                  task_id,
    TASK_SOURCE         *thread_source, 
    TASK_DATA               *task_data, 
    MEMORY_ARENA         *thread_arena, 
    WIN32_THREAD_ARGS       *main_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{
    UNUSED_ARG(task_id);
    UNUSED_ARG(thread_source);
    UNUSED_ARG(thread_arena);
    UNUSED_ARG(main_args);
    UNUSED_ARG(scheduler);
    ASYNC_TASK_DATA *args = (ASYNC_TASK_DATA*) task_data->Data;
    uint64_t tss = TimestampInTicks();
    WaitForSingleObject(args->Signal, INFINITE);
    uint64_t tse = TimestampInTicks();
    uint64_t  dt = ElapsedNanoseconds(tss, tse);
    ConsoleOutput("STATUS (%S): Waited %0.03fms.\n", __FUNCTION__, dt / 1000000.0);
}

internal_function void
AsyncLaunchesComputeTaskMain
(
    task_id_t                  task_id,
    TASK_SOURCE         *thread_source, 
    TASK_DATA               *task_data, 
    MEMORY_ARENA         *thread_arena, 
    WIN32_THREAD_ARGS       *main_args,
    WIN32_TASK_SCHEDULER    *scheduler
)
{   UNUSED_ARG(task_id);
    UNUSED_ARG(scheduler);
    UNUSED_ARG(main_args);

    // allocate some thread-local memory. up to 8MB are available.
    // this memory is automatically freed when the task entrypoint exits.
    // TODO(rlk): provide a function to get the maximum memory that can be allocated.
    ASYNC_TASK_DATA *args = (ASYNC_TASK_DATA*) task_data->Data;
    size_t    count     = Megabytes(5);
    uint8_t  *array     = PushArray<uint8_t>(thread_arena, count);
    HANDLE       ev     = CreateEvent(NULL, FALSE, FALSE, NULL);
    
    // set up a local batch used to create new tasks.
    // the batch is automatically flushed as-needed and when it goes out of scope.
    {   
        TASK_BATCH    b = {};
        NewTaskBatch(&b, thread_source);
        LAUNCH_TASK_DATA ld = { array, count, 1024 };
        task_id_t launch_id = NewComputeTask(&b, LaunchTaskMain, &ld, TASK_SIZE_SMALL);
        FENCE_TASK_DATA  fd = { array, count, ev };
        task_id_t finish_id = NewComputeTask(&b, FenceTaskMain , &fd, &launch_id, 1, TASK_SIZE_LARGE);
        UNUSED_LOCAL(finish_id);
    }

    WaitForSingleObject(ev, INFINITE);
    CloseHandle(ev);
    SetEvent(args->Signal);
}

/// @summary Initializes a command line arguments definition with the default program configuration.
/// @param args The command line arguments definition to populate.
/// @return true if the input command line arguments structure is valid.
internal_function bool
DefaultCommandLineArguments
(
    WIN32_COMMAND_LINE *args
)
{
    if (args != NULL)
    {   // set the default command-line argument values.
        args->CreateConsole = false;
        return true;
    }
    return false;
}

/// @summary Parse the command line specified when the application was launched.
/// @param args The command-line argument options to populate.
/// @return true if the command-line arguments were parsed.
internal_function bool
ParseCommandLine
(
    WIN32_COMMAND_LINE *args
)
{
    //command_line_args_t cl = {};
    LPTSTR  command_line   = GetCommandLine();
    int     argc           = 1;
#if defined(_UNICODE) || defined(UNICODE)
    LPTSTR *argv           = CommandLineToArgvW(command_line, &argc);
#else
    LPTSTR *argv           = CommandLineToArgvA(command_line, &argc);
#endif

    if (argc <= 1)
    {   // no command-line arguments were specified. return the default configuration.
        LocalFree((HLOCAL) argv);
        return DefaultCommandLineArguments(args);
    }

    for (size_t i = 1, n = size_t(argc); i < n; ++i)
    {
        TCHAR *arg = argv[i];
        TCHAR *key = NULL;
        TCHAR *val = NULL;

        if (ArgumentKeyAndValue(arg, &key, &val)) // may mutate arg
        {
            if (KeyMatch(key, _T("c")) || KeyMatch(key, _T("console")))
            {   // create a console window to view debug output.
                args->CreateConsole = true;
            }
            else
            {   // the key is not recognized. output a debug message, but otherwise ignore the error.
                DebugPrintf(_T("Unrecognized command-line option %Id; got key = %s and val = %s.\n"), i, key, val);
            }
        }
        else
        {   // the key-value pair could not be parsed. output a debug message, but otherwise ignore the error.
            DebugPrintf(_T("Unparseable command-line option %Id; got key = %s and val = %s.\n"), i, key, val);
        }
    }

    // finished with command line argument parsing; clean up and return.
    LocalFree((HLOCAL) argv);
    return true;
}

/// @summary Attaches a console window to the application. This is useful for viewing debug output.
internal_function void
CreateConsoleAndRedirectStdio
(
    void
)
{
    HANDLE console_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
    bool          is_file = GetFileType (console_stdout) == FILE_TYPE_DISK;
    // is_file is true if stdout/stderr are being redirected to a file.

    if (AllocConsole())
    {   // a process can only have one console associated with it. this function just 
        // allocated that console, so perform the buffer setup and I/O redirection.
        // the default size is 120 characters wide and 30 lines tall.
        CONSOLE_SCREEN_BUFFER_INFO   buffer_info;
        SHORT const                lines_visible = 9999;
        SHORT const                chars_visible = 120;
        FILE                             *conout = NULL;
        FILE                              *conin = NULL;

        // set up the console size to be the Windows default of 120x30.
        GetConsoleScreenBufferInfo(console_stdout, &buffer_info);
        buffer_info.dwSize.X = chars_visible;
        buffer_info.dwSize.Y = lines_visible;
        SetConsoleScreenBufferSize(console_stdout, buffer_info.dwSize);

        if (!is_file)
        {   // console output is not being redirected, so re-open the streams.
            freopen_s(&conin , "conin$" , "r", stdin);
            freopen_s(&conout, "conout$", "w", stdout);
            freopen_s(&conout, "conout$", "w", stderr);
            // synchronize everything that uses <iostream>.
            std::ios::sync_with_stdio();
        }
    }
}

/// @summary WndProc for the hidden message-only window on the main thread.
/// @param hwnd The handle of the window to which the message was sent.
/// @param message The message identifier.
/// @param wparam Additional message-specific data.
/// @param lparam Additional message-specific data.
/// @return A message-specific result code.
internal_function LRESULT CALLBACK
MessageWindowCallback
(
    HWND   hwnd, 
    UINT   message, 
    WPARAM wparam, 
    LPARAM lparam
)
{   // WM_NCCREATE performs special handling to store the WIN32_THREAD_ARGS pointer in the user data of the window.
    // the handler for WM_NCCREATE executes before the call to CreateWindowEx returns in CreateMessageWindow.
    if (message == WM_NCCREATE)
    {   // store the WIN32_THREAD_ARGS in the window user data.
        CREATESTRUCT *cs  = (CREATESTRUCT*) lparam;
        SetWindowLongPtr(hwnd, GWLP_USERDATA, (LONG_PTR) cs->lpCreateParams);
        return DefWindowProc(hwnd, message, wparam, lparam);
    }

    // process all other messages sent (or posted) to the window.
    WIN32_THREAD_ARGS *thread_args = (WIN32_THREAD_ARGS*) GetWindowLongPtr(hwnd, GWLP_USERDATA);
    LRESULT result = 0;
    switch (message)
    {
        case WM_DESTROY:
            {   // termination was signaled from another thread. 
                // post WM_QUIT to terminate the main time loop.
                PostQuitMessage(0);
            } break;

        case WM_INPUT:
            {   // a fixed-size buffer on the stack should be sufficient for storing the packet.
                UINT const packet_size = 256;
                UINT       packet_used = packet_size;
                uint8_t    packet[packet_size];
                if (GetRawInputData((HRAWINPUT) lparam, RID_INPUT, packet, &packet_used, sizeof(RAWINPUTHEADER)) > 0)
                {   // wparam is RIM_INPUT (foreground) or RIM_INPUTSINK (background).
                    PushRawInput(thread_args->InputSystem, (RAWINPUT*) packet);
                    result = DefWindowProc(hwnd, message, wparam, lparam);
                }
                else
                {   // there was an error retrieving data; pass the message on to the default handler.
                    result = DefWindowProc(hwnd, message, wparam, lparam);
                }
            } break;

        case WM_INPUT_DEVICE_CHANGE:
            {   // a keyboard or mouse was attached or removed.
                PushRawInputDeviceChange(thread_args->InputSystem, wparam, lparam);
            } break;

        case WM_ACTIVATE:
            {   // the application is being activated or deactivated. adjust the system scheduler frequency accordingly.
                if (wparam)
                {
                    ConsoleOutput("STATUS: Increase scheduler frequency to 1ms.\n");
                    timeBeginPeriod(1);
                }
                else
                {
                    ConsoleOutput("STATUS: Decrease scheduler frequency to default.\n");
                    timeEndPeriod(1);
                }
            } break;

        default:
            {   // pass the message on to the default handler:
                result = DefWindowProc(hwnd, message, wparam, lparam);
            } break;
    }
    return result;
}

/// @summary Create a message-only window for the sole purpose of gathering user input and receiving messages from other threads.
/// @param this_instance The HINSTANCE of the application passed to WinMain.
/// @param thread_args The global data passed to all threads. This data is also available to the message window.
/// @return The handle of the message-only window used to communicate with the main thread and gather user input.
internal_function HWND 
CreateMessageWindow
(
    HINSTANCE         this_instance, 
    WIN32_THREAD_ARGS  *thread_args
)
{
    TCHAR const *class_name = _T("GM2016_MessageWindow");
    WNDCLASSEX     wndclass = {};

    // register the window class, if necessary.
    if (!GetClassInfoEx(this_instance, class_name, &wndclass))
    {   // the window class hasn't been registered yet.
        wndclass.cbSize         = sizeof(WNDCLASSEX);
        wndclass.cbClsExtra     = 0;
        wndclass.cbWndExtra     = 0;
        wndclass.hInstance      = this_instance;
        wndclass.lpszClassName  = class_name;
        wndclass.lpszMenuName   = NULL;
        wndclass.lpfnWndProc    = MessageWindowCallback;
        wndclass.hIcon          = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hIconSm        = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hCursor        = LoadCursor(0, IDC_ARROW);
        wndclass.style          = 0;
        wndclass.hbrBackground  = NULL;
        if (!RegisterClassEx(&wndclass))
        {   // unable to register the window class; cannot proceed.
            DebugPrintf(_T("ERROR: Unable to register the message window class.\n"));
            return NULL;
        }
    }

    // the message window spans the entire virtual display space across all monitors.
    int x = GetSystemMetrics(SM_XVIRTUALSCREEN);
    int y = GetSystemMetrics(SM_YVIRTUALSCREEN);
    int w = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    int h = GetSystemMetrics(SM_CYVIRTUALSCREEN);

    // return the window handle. the window is not visible.
    HWND hwnd  = CreateWindowEx(0, class_name, class_name, 0, x, y, w, h, HWND_MESSAGE, NULL, this_instance, thread_args);
    if  (hwnd != NULL)
    {   // the message window was successfully created. save the handle in thread_args.
        // the thread_args value is stored in the user data pointer of the window. see WM_CREATE in MessageWindowCallback.
        thread_args->MessageWindow = hwnd;
        return hwnd;
    }
    else
    {   // if the message window could not be created, execution cannot continue.
        DebugPrintf(_T("ERROR: Unable to create the message window; reason = 0x%08X.\n"), GetLastError());
        return NULL;
    }
}

/// @summary Performs all setup and initialization for the input system. User input is handled on the main thread.
/// @param message_window The handle of the main thread's message window.
/// @return true if the input system is initialized and ready for use.
internal_function bool 
SetupInputDevices
(
    HWND message_window
)
{   // http://www.usb.org/developers/hidpage/Hut1_12v2.pdf
    RAWINPUTDEVICE keyboard_and_mouse[2] = { 
        { 1, 6, RIDEV_DEVNOTIFY, message_window }, // keyboard
        { 1, 2, RIDEV_DEVNOTIFY, message_window }  // mouse
    };

    // this should also create a Windows message queue for the thread.
    if (!RegisterRawInputDevices(keyboard_and_mouse, 2, sizeof(RAWINPUTDEVICE)))
    {
        DebugPrintf(_T("ERROR: Unable to register for keyboard and mouse input; reason = 0x%08X.\n"), GetLastError());
        return false;
    }
    // XInput is loaded by the InitializeRuntime function in win32_runtime.cc.
    return true;
}

/// @summary Spawn an explicitly-managed thread.
/// @param thread_main The thread entry point.
/// @param thread_args Data to pass to the thread being spawned.
/// @return The handle of the new thread, or NULL if the thread cannot be created.
internal_function HANDLE
SpawnExplicitThread
(
    WIN32_THREAD_ENTRYPOINT thread_main, 
    WIN32_THREAD_ARGS      *thread_args
)
{
    return (HANDLE) _beginthreadex(NULL, 0, thread_main, thread_args, 0, NULL);
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Implements the entry point of the application.
/// @param this_instance The module base address of the executable
/// @param prev_instance Unused. This value is always NULL.
/// @param command_line The zero-terminated command line string.
/// @param show_command One of SW_MAXIMIZED, etc. indicating how the main window should be displayed.
/// @return The application exit code, or 0 if the application terminates before entering the main message loop.
export_function int WINAPI
WinMain
(
    HINSTANCE this_instance, 
    HINSTANCE prev_instance, 
    LPSTR      command_line, 
    int        show_command
)
{
    WIN32_COMMAND_LINE                       argv;
    WIN32_THREAD_ARGS                 thread_args = {};
    WIN32_MEMORY_ARENA              main_os_arena = {};
    WIN32_INPUT_EVENTS               input_events = {};
    WIN32_INPUT_SYSTEM               input_system = {};
    WIN32_CPU_INFO                       host_cpu = {};
    MEMORY_ARENA                       main_arena = {};
    WIN32_TASK_SCHEDULER_CONFIG  scheduler_config = {};
    WIN32_TASK_SCHEDULER           task_scheduler = {};
    HANDLE                               ev_start = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE                               ev_break = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE                               ev_fence = CreateEvent(NULL, FALSE,FALSE, NULL); // auto-reset
    HANDLE                            thread_draw = NULL; // frame composition thread and main UI thread
    HANDLE                            thread_disk = NULL; // asynchronous disk I/O thread
    HANDLE                            thread_net  = NULL; // network I/O thread
    uint64_t                predicted_tick_launch = 0;    // the ideal launch time of the next tick
    uint64_t                   actual_tick_launch = 0;    // the actual launch time of the current tick
    uint64_t                   actual_tick_finish = 0;    // the actual finish time of the current tick
    int64_t                        tick_miss_time = 0;    // number of nanoseconds over the launch time
    DWORD                               wait_time = 0;    // number of milliseconds the timer thread will sleep for
    HWND                           message_window = NULL; // for receiving input and notification from other threads
    bool                             keep_running = true;
    uint64_t const                  tick_interval = SliceOfSecond(60);
    size_t   const                main_arena_size = Megabytes(256);
    size_t   const              default_alignment = std::alignment_of<void*>::value;

    size_t const                   workspace_size = Megabytes(16);
    uint8_t                            *workspace = NULL;

    UNUSED_ARG(prev_instance);
    UNUSED_ARG(command_line);
    UNUSED_ARG(show_command);

    // set up the runtime environment. if any of these steps fail, the game cannot run.
    if (!ParseCommandLine(&argv))
    {   // bail out if the command line cannot be parsed successfully.
        DebugPrintf(_T("ERROR: Unable to parse the command line.\n"));
        return 0;
    }
    if (!InitializeRuntime(WIN32_RUNTIME_TYPE_CLIENT))
    {   // if the necessary privileges could not be obtained, there's no point in proceeding.
        goto cleanup_and_shutdown;
    }

    // set up the global memory arena.
    if (CreateMemoryArena(&main_os_arena, main_arena_size) < 0)
    {
        DebugPrintf(_T("ERROR: Unable to allocate the required global memory.\n"));
        goto cleanup_and_shutdown;
    }
    if (CreateArena(&main_arena, main_arena_size, default_alignment, &main_os_arena) < 0)
    {
        DebugPrintf(_T("ERROR: Unable to initialize the global memory arena.\n"));
        goto cleanup_and_shutdown;
    }

    workspace = PushArray<uint8_t>(&main_arena, workspace_size);

    // initialize low-level services for timing, user input, etc.
    // initialize the data that is available to all worker threads.
    // the MessageWindow field of thread_args is set by CreateMessageWindow.
    QueryClockFrequency();
    EnumerateHostCPU(&host_cpu);
    ResetInputSystem(&input_system);
    thread_args.StartEvent     = ev_start;
    thread_args.TerminateEvent = ev_break;
    thread_args.ModuleBaseAddr = this_instance;
    thread_args.MessageWindow  = NULL;
    thread_args.HostCPUInfo    = &host_cpu;
    thread_args.CommandLine    = &argv;
    thread_args.InputSystem    = &input_system;
    thread_args.TaskScheduler  = &task_scheduler;

    // create the message window used to receive user input and messages from other threads.
    if ((message_window = CreateMessageWindow(this_instance, &thread_args)) == NULL)
    {   // without the message window, no user input can be processed.
        goto cleanup_and_shutdown;
    }
    if (argv.CreateConsole)
    {   // for some reason, this has to be done *after* the message window is created.
        CreateConsoleAndRedirectStdio();
    }

    // register for Raw Input from keyboard and pointer devices.
    if (!SetupInputDevices(message_window))
    {   // no user input services are available.
        goto cleanup_and_shutdown;
    }

    // create the compute task scheduler first, and then the async scheduler.
    // the async scheduler may depend on the compute task scheduler.
    DefaultSchedulerConfiguration(&scheduler_config, &thread_args, &host_cpu);
    scheduler_config.PoolSize[TASK_POOL_GENERAL].MaxTasks = 8192;
    scheduler_config.PoolSize[TASK_POOL_COMPUTE].MaxTasks = 65536;
    if (CreateScheduler(&task_scheduler, &scheduler_config, &main_arena) < 0)
    {   // no compute task scheduler is available.
        ConsoleError("ERROR: Unable to create the compute task scheduler.\n");
        goto cleanup_and_shutdown;
    }

    // set up explicit threads for frame composition, network I/O and file I/O.
    if ((thread_draw = SpawnExplicitThread(DisplayThread, &thread_args)) == NULL)
    {
        ConsoleError("ERROR: Unable to spawn the display thread.\n");
        goto cleanup_and_shutdown;
    }

    // start all of the explicit threads running:
    SetEvent(ev_start);

    // start all of the worker threads running.
    LaunchScheduler(&task_scheduler);

    // grab an initial absolute timestamp and initialize global time values.
    predicted_tick_launch = TimestampInNanoseconds();
    actual_tick_launch    = predicted_tick_launch;
    actual_tick_finish    = predicted_tick_launch;
    tick_miss_time        = 0;
    wait_time             = 0;

    // enter the main game loop:
    while (keep_running)
    {   
        MSG msg;

        // check to see if termination has been signaled by an external thread.
        if (WaitForSingleObject(ev_break, 0) == WAIT_OBJECT_0)
        {   // termination signalled externally.
            DestroyWindow(message_window);
        }

        // poll the Windows message queue for the thread to receive WM_INPUT and other critical notifications.
        // unfortunately, this needs to be done before launching the tick in order to minimize input latency.
        // typically work should be done *after* the tick is launched, for maximum launch accuracy.
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE) && keep_running)
        {   // dispatch the message to the MessageWindowCallback.
            switch (msg.message)
            {
                case WM_QUIT:
                    {   // the message window is destroyed. terminate the application.
                        keep_running = false;
                    } break;

                default:
                    {   // forward the message on to MessageWindowCallback.
                        TranslateMessage(&msg);
                        DispatchMessage (&msg);
                    } break;
            }
        }
        if (!keep_running)
        {   // time to break out of the main loop.
            break;
        }

        // immediately launch the current frame.
        // so, this here is the tick launch. we have the predicted tick launch, and the actual tick launch.
        // the difference between those two is the miss time, which may be positive or negative.
        // the predicted (next) tick launch is the predicted (prev) tick launch + tick_interval.
        // we also have the tick end time. the predicted (next) tick launch minus the tick end time (signed) is the wait time.
        // if the wait time is > 1ms, then sleep.
        // TODO(rlk): miss_time can be used to calculate an interpolation factor, it tells us how far into the current tick we are.
        actual_tick_launch = TimestampInNanoseconds();
        tick_miss_time     = predicted_tick_launch - actual_tick_launch;
        // if the thread woke up slightly early, burn CPU time to avoid launching early.
        /*while (actual_tick_launch < predicted_tick_launch)
        {   // too bad there's no way to see this bit in a profiler...
            actual_tick_launch = TimestampInNanoseconds();
        }*/
        // account for the case where the wakeup missed its target by more than one tick.
        while (tick_miss_time >= int64_t(tick_interval))
        {   // fast-forward until we reach the 'current' tick.
            predicted_tick_launch += tick_interval;
            tick_miss_time        -= tick_interval;
        }
        // calculate the predicted launch time of the following tick.
        predicted_tick_launch = predicted_tick_launch + tick_interval;

        // work work work
        ConsoleOutput("Launch tick at %0.06f, next at %0.06f, miss by %I64dns (%0.06fms).\n", NanosecondsToWholeMilliseconds(actual_tick_launch) / 1000.0, NanosecondsToWholeMilliseconds(predicted_tick_launch) / 1000.0, tick_miss_time, tick_miss_time / 1000000.0);
        TASK_BATCH    b  = {};
        NewTaskBatch(&b, RootTaskSource(&task_scheduler));

        /*
        LAUNCH_TASK_DATA launch_data = { workspace, workspace_size, 2048 };
        FENCE_TASK_DATA   fence_data = { workspace, workspace_size, ev_fence };
        task_id_t        launch_task = NewComputeTask(&b, LaunchTaskMain, &launch_data, TASK_SIZE_SMALL);
        task_id_t        finish_task = NewComputeTask(&b,  FenceTaskMain,  &fence_data, &launch_task, 1, TASK_SIZE_LARGE);
        FlushTaskBatch(&b);
        WaitForSingleObject(ev_fence, INFINITE);
        UNUSED_LOCAL(finish_task);
        */

        ASYNC_TASK_DATA ad = { ev_fence };
        task_id_t async_id = NewGeneralTask(&b, AsyncLaunchesComputeTaskMain, &ad);
        UNUSED_LOCAL(async_id);
        FlushTaskBatch(&b);
        WaitForSingleObject(ev_fence, INFINITE);
        UNUSED_LOCAL(workspace);

        // all of the work for this thread for the current tick has completed.
        if ((actual_tick_finish = TimestampInNanoseconds()) < predicted_tick_launch)
        {   // this tick has finished early. how long should we sleep for?
            if ((wait_time = NanosecondsToWholeMilliseconds(predicted_tick_launch - actual_tick_finish)) > 1)
            {   // put the thread to sleep for at least 1ms.
                ConsoleOutput("Sleep for at least %ums.\n", wait_time);
                Sleep(wait_time);
            }
        }
        UNUSED_LOCAL(input_events);
    }

    ConsoleOutput("The main thread has exited.\n");

cleanup_and_shutdown:
    HaltScheduler(&task_scheduler);
    if (ev_break        != NULL) SetEvent(ev_break);
    if (thread_draw     != NULL) WaitForSingleObject(thread_draw, INFINITE);
    if (thread_disk     != NULL) WaitForSingleObject(thread_disk, INFINITE);
    if (thread_net      != NULL) WaitForSingleObject(thread_net , INFINITE);
    if (argv.CreateConsole)
    {
        printf("\nApplication terminated. Press any key to exit.\n");
        (void) _getch();
    }
    CloseHandle(thread_net);
    CloseHandle(thread_disk);
    CloseHandle(thread_draw);
    CloseHandle(ev_break);
    CloseHandle(ev_start);
    return 0;
}

