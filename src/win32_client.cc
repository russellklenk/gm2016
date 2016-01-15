/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the client application. Initializes 
/// the runtime and sets up the execution environment for the explicit 
/// background threads. User input is handled on the main thread.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <iostream>

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

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
#include "win32_timestamp.cc"
#include "win32_parse.cc"
#include "win32_memarena.cc"
#include "win32_render.cc"
#include "win32_input.cc"

#include "memarena.cc"

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
    if (AllocConsole())
    {   // a process can only have one console associated with it. this function just 
        // allocated that console, so perform the buffer setup and I/O redirection.
        // the default size is 120 characters wide and 30 lines tall.
        CONSOLE_SCREEN_BUFFER_INFO    buffer_info;
        SHORT const                 lines_visible = 9999;
        SHORT const                 chars_visible = 120;
        HANDLE                      console_stdin = GetStdHandle(STD_INPUT_HANDLE);
        HANDLE                     console_stdout = GetStdHandle(STD_OUTPUT_HANDLE);
        HANDLE                     console_stderr = GetStdHandle(STD_ERROR_HANDLE);

        // set up the console size to be the Windows default of 120x30.
        GetConsoleScreenBufferInfo(console_stdout, &buffer_info);
        buffer_info.dwSize.X = chars_visible;
        buffer_info.dwSize.Y = lines_visible;
        SetConsoleScreenBufferSize(console_stdout,  buffer_info.dwSize);

        // redirect stdin to the new console window:
        int      new_stdin   = _open_osfhandle((intptr_t) console_stdin , _O_TEXT);
        FILE     *fp_stdin   = _fdopen(new_stdin, "r");
        *stdin = *fp_stdin;  setvbuf(stdin, NULL, _IONBF, 0);

        // redirect stdout to the new console window:
        int      new_stdout  = _open_osfhandle((intptr_t) console_stdout, _O_TEXT);
        FILE     *fp_stdout  = _fdopen(new_stdout, "w");
        *stdout= *fp_stdout; setvbuf(stdout, NULL, _IONBF, 0);

        // redirect stderr to the new console window:
        int      new_stderr  = _open_osfhandle((intptr_t) console_stderr, _O_TEXT);
        FILE     *fp_stderr  = _fdopen(new_stderr, "w");
        *stderr= *fp_stderr; setvbuf(stderr, NULL, _IONBF, 0);

        // synchronize everything that uses <iostream>.
        std::ios::sync_with_stdio();
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
{
    LRESULT result = 0;
    switch (message)
    {
        case WM_DESTROY:
            {   // termination was signaled from another thread. 
                // post WM_QUIT to terminate the main game loop.
                PostQuitMessage(0);
            } break;

        case WM_INPUT:
            {   // a fixed-size buffer on the stack should be sufficient for storing the packet.
                UINT       packet_used = 0;
                UINT const packet_size = 256;
                uint8_t    packet[packet_size];

                if (GetRawInputData((HRAWINPUT) lparam, RID_INPUT, packet, &packet_used, sizeof(RAWINPUTHEADER)) > 0)
                {   // wparam is RIM_INPUT (foreground) or RIM_INPUTSINK (background).
                    RAWINPUT *input = (RAWINPUT*)packet;

                    switch (input->header.dwType)
                    {
                        case RIM_TYPEKEYBOARD:
                            {
                            } break;

                        case RIM_TYPEMOUSE:
                            {
                            } break;

                        default:
                            {   // unknown device type; ignore.
                            } break;
                    }

                    // pass the message on to the default handler "for cleanup".
                    result = DefWindowProc(hwnd, message, wparam, lparam);
                }
                else
                {   // there was an error retrieving data; pass the message on to the default handler.
                    result = DefWindowProc(hwnd, message, wparam, lparam);
                }
            } break;

        case WM_INPUT_DEVICE_CHANGE:
            {   // a keyboard or mouse was attached or removed.
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
/// @return The handle of the message-only window used to communicate with the main thread and gather user input.
internal_function HWND 
CreateMessageWindow
(
    HINSTANCE this_instance
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
    return CreateWindowEx(0, class_name, class_name, 0, x, y, w, h, HWND_MESSAGE, NULL, this_instance, NULL);
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
    WIN32_COMMAND_LINE        argv;
    WIN32_THREAD_ARGS  thread_args = {};
    HANDLE                ev_start = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE                ev_break = CreateEvent(NULL, TRUE, FALSE, NULL); // manual-reset
    HANDLE             thread_draw = NULL; // frame composition thread and main UI thread
    HANDLE             thread_disk = NULL; // asynchronous disk I/O thread
    HANDLE             thread_net  = NULL; // network I/O thread
    HWND            message_window = NULL; // for receiving input and notification from other threads
    bool              keep_running = true;

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
    if ((message_window = CreateMessageWindow(this_instance)) == NULL)
    {   // without the message window, no user input can be processed.
        goto cleanup_and_shutdown;
    }
    if (!SetupInputDevices(message_window))
    {   // no user input services are available.
        goto cleanup_and_shutdown;
    }
    if (argv.CreateConsole)
    {   // for some reason, this has to be done *after* the message window is created.
        CreateConsoleAndRedirectStdio();
    }
    // ConsoleError and ConsoleOutput should be used past this point.
    // 

    // set up explicit threads for frame composition, network I/O and file I/O.
    thread_args.StartEvent     = ev_start;
    thread_args.TerminateEvent = ev_break;
    thread_args.ModuleBaseAddr = this_instance;
    thread_args.MessageWindow  = message_window;
    thread_args.CommandLine    =&argv;
    if ((thread_draw = SpawnExplicitThread(RenderThread, &thread_args)) == NULL)
    {
        ConsoleError("ERROR: Unable to spawn the rendering thread.\n");
        goto cleanup_and_shutdown;
    }

    // start all of the explicit threads running:
    SetEvent(ev_start);

    // enter the main game loop:
    while (keep_running)
    {   
        MSG msg;

        // poll for externally-signaled application termination.
        if (WaitForSingleObject(ev_break, 0) == WAIT_OBJECT_0)
        {   // some other thread (probably the render thread) has signaled termination.
            DestroyWindow(message_window);
        }

        // poll the Windows message queue for the thread to receive WM_INPUT notifications.
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
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
    }

    ConsoleOutput("The main thread has exited.\n");

cleanup_and_shutdown:
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

