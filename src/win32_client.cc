/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the client application. Initializes 
/// the runtime and sets up the execution environment for the explicit 
/// background threads. User input is handled on the main thread.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <iostream>
#include <atomic>

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <assert.h>

#include <process.h>
#include <conio.h>
#include <fcntl.h>
#include <io.h>

#include <tchar.h>
#include <Windows.h>
#include <Shellapi.h>
#include <XInput.h>

#include "platform_config.h"
#include "compiler_config.h"
#include "win32_platform.h"

#include "win32_debug.cc"
#include "win32_runtime.cc"
#include "win32_memarena.cc"
#include "win32_timestamp.cc"
#include "win32_tasksched.cc"
#include "win32_parse.cc"
#include "win32_display.cc"
#include "win32_input.cc"

/*//////////////////
//   Data Types   //
//////////////////*/

/*///////////////
//   Globals   //
///////////////*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
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
    WIN32_COMMAND_LINE              argv;
    WIN32_THREAD_ARGS        thread_args = {};
    WIN32_MEMORY_ARENA     main_os_arena = {};
    WIN32_INPUT_EVENTS      input_events = {};
    WIN32_INPUT_SYSTEM      input_system = {};
    WIN32_CPU_INFO              host_cpu = {};
    WIN32_TASK_SCHEDULER *task_scheduler = NULL;
    MEMORY_ARENA              main_arena = {};
    HANDLE                      ev_start = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE                      ev_break = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE                   thread_draw = NULL; // frame composition thread and main UI thread
    HANDLE                   thread_disk = NULL; // asynchronous disk I/O thread
    HANDLE                   thread_net  = NULL; // network I/O thread
    uint64_t                   next_tick = 0;    // the ideal launch time of the next tick
    uint64_t                current_tick = 0;    // the launch time of the current tick
    uint64_t               previous_tick = 0;    // the launch time of the previous tick
    uint64_t                   miss_time = 0;    // number of nanoseconds over the launch time
    DWORD                      wait_time = 0;    // number of milliseconds the timer thread will sleep for
    HWND                  message_window = NULL; // for receiving input and notification from other threads
    bool                    keep_running = true;

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
    if (CreateMemoryArena(&main_os_arena, 32 * 1024 * 1024) < 0)
    {
        DebugPrintf(_T("ERROR: Unable to allocate the required global memory.\n"));
        goto cleanup_and_shutdown;
    }
    if (CreateArena(&main_arena, 32 * 1024 * 1024, sizeof(intptr_t), &main_os_arena) < 0)
    {
        DebugPrintf(_T("ERROR: Unable to initialize the global memory arena.\n"));
        goto cleanup_and_shutdown;
    }

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

    // set up explicit threads for frame composition, network I/O and file I/O.
    if ((thread_draw = SpawnExplicitThread(DisplayThread, &thread_args)) == NULL)
    {
        ConsoleError("ERROR: Unable to spawn the display thread.\n");
        goto cleanup_and_shutdown;
    }
    // TODO(rlk): Need to separate into Make and Launch steps - Make up above, launch here.
    if ((task_scheduler = MakeTaskScheduler(&main_arena, &thread_args)) == NULL)
    {
        ConsoleError("ERROR: Unable to launch the task scheduler.\n");
        goto cleanup_and_shutdown;
    }

    // start all of the explicit threads running:
    SetEvent(ev_start);

    // grab an initial absolute timestamp and initialize global time values.
    previous_tick = TimestampInNanoseconds();
    next_tick     = previous_tick;
    miss_time     = 0;
    wait_time     = 0;

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
        // TODO(rlk): miss_time can be used to calculate an interpolation factor, it tells us how far into the current tick we are.
        current_tick  = TimestampInNanoseconds();
        if (current_tick < next_tick)
        {   // the tick is launching slightly early.
            miss_time = next_tick - current_tick;
            next_tick =(miss_time + current_tick) + SliceOfSecond(60);
        }
        else
        {   // the tick is launching slightly late.
            miss_time = current_tick - next_tick;
            next_tick =(current_tick - miss_time) + SliceOfSecond(60);
        }
        ConsoleOutput("Launch tick at %0.06f, next at %0.06f, miss by %Iuns (%0.06fms).\n", NanosecondsToWholeMilliseconds(current_tick) / 1000.0, NanosecondsToWholeMilliseconds(next_tick) / 1000.0, miss_time, miss_time / 1000000.0);

        // all work for the current tick has completed.
        if ((previous_tick = TimestampInNanoseconds()) < next_tick)
        {   // this tick has finished early. how long should we sleep for?
            if ((wait_time = NanosecondsToWholeMilliseconds(next_tick - previous_tick)) > 1)
            {   // put the thread to sleep for at least 1ms.
                //ConsoleOutput("Sleep for at least %ums.\n", wait_time);
                Sleep(wait_time);
            }
        }
        UNUSED_LOCAL(input_events);
    }

    ConsoleOutput("The main thread has exited.\n");

cleanup_and_shutdown:
    // TODO(rlk): cleanup for the task scheduler
    if (ev_break    != NULL) SetEvent(ev_break);
    if (thread_draw != NULL) WaitForSingleObject(thread_draw, INFINITE);
    if (thread_disk != NULL) WaitForSingleObject(thread_disk, INFINITE);
    if (thread_net  != NULL) WaitForSingleObject(thread_net , INFINITE);
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

