/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the client application. Initializes 
/// the runtime and sets up the rendering system, which is performed on the 
/// main thread. User input and networking are handled on explicit background
/// threads.
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

#include "platform_config.h"
#include "compiler_config.h"

#include "win32_platform.h"
#include "win32_runtime.cc"
#include "win32_timestamp.cc"
#include "win32_debug.cc"
#include "win32_parse.cc"
#include "win32_memarena.cc"
#include "win32_render.cc"

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
    return (HANDLE) _beginthreadex(NULL, 0, (_beginthreadex_proc_type) thread_main, thread_args, 0, NULL);
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

    UNUSED_ARG(prev_instance);
    UNUSED_ARG(command_line);
    UNUSED_ARG(show_command);

    // set up the runtime environment. if any of these steps fail, the game cannot run.
    if (!ParseCommandLine(&argv))
    {   // bail out if the command line cannot be parsed successfully.
        DebugPrintf(_T("ERROR: Unable to parse the command line.\n"));
        return 0;
    }
    if (argv.CreateConsole)
    {   // create the console before doing anything else, so all debug output can be seen.
        CreateConsoleAndRedirectStdio();
    }
    if (!Win32InitializeRuntime())
    {   // if the necessary privileges could not be obtained, there's no point in proceeding.
        goto cleanup_and_shutdown;
    }

    // set up explicit threads for frame composition, network I/O and file I/O.
    thread_args.StartEvent     = ev_start;
    thread_args.TerminateEvent = ev_break;
    thread_args.ModuleBaseAddr = this_instance;
    thread_args.CommandLine    =&argv;
    if ((thread_draw = SpawnExplicitThread(RenderThread, &thread_args)) == NULL)
    {
        ConsoleError("ERROR: Unable to spawn the rendering thread.\n");
        goto cleanup_and_shutdown;
    }

    // start all of the explicit threads running:
    SetEvent(ev_start);

    // enter the main game loop:
    for ( ; ; )
    {   // TODO(rlk): change the 8 to 0 - it's only 8 to simulate the main thread's "work".
        if (WaitForSingleObject(ev_break, 8) == WAIT_OBJECT_0)
        {   // some other thread (probably the render thread) has signaled termination.
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

