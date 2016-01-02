/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the application.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <iostream>

#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <conio.h>
#include <fcntl.h>
#include <io.h>

#include <tchar.h>
#include <windows.h>
#include <shellapi.h>

#include "platform_config.h"
#include "compiler_config.h"

#include "win32_debug.cc"
#include "win32_parse.cc"
#include "win32_memarena.cc"

#include "memarena.cc"

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Defines the data associated with a parsed command line.
struct COMMAND_LINE
{
    bool CreateConsole;
};

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Initializes a command line arguments definition with the default program configuration.
/// @param args The command line arguments definition to populate.
/// @return true if the input command line arguments structure is valid.
internal_function bool
DefaultCommandLineArguments
(
    COMMAND_LINE *args
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
    COMMAND_LINE *args
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

/// @summary Enable or disable a process privilege.
/// @param token The privilege token of the process to modify.
/// @param privilege_name The name of the privilege to enable or disable.
/// @param should_enable Specify TRUE to request the privilege, or FALSE to disable the privilege.
internal_function bool
EnableProcessPrivilege
(
    HANDLE           token, 
    LPCTSTR privilege_name,
    BOOL     should_enable
)
{
    TOKEN_PRIVILEGES tp;
    LUID           luid;

    if (LookupPrivilegeValue(NULL, privilege_name, &luid))
    {
        tp.PrivilegeCount           = 1;
        tp.Privileges[0].Luid       = luid;
        tp.Privileges[0].Attributes = should_enable ? SE_PRIVILEGE_ENABLED : 0;
        if (AdjustTokenPrivileges(token, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), NULL, NULL))
        {   // the requested privilege adjustment was made successfully.
            return (GetLastError() != ERROR_NOT_ALL_ASSIGNED);
        }
    }
    return false;
}

/// @summary Request any privilege elevations for the current process.
/// @return true if the necessary privileges have been obtained.
internal_function bool
ElevateProcessPrivileges
(
    void
)
{
    HANDLE token;
    if (OpenProcessToken(GetCurrentProcess(), TOKEN_ALL_ACCESS, &token))
    {
        bool se_debug = EnableProcessPrivilege(token, SE_DEBUG_NAME, TRUE);
        // ...
        CloseHandle(token);
        return (se_debug);
    }
    return false;
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
    COMMAND_LINE argv;

    UNUSED_ARG(this_instance);
    UNUSED_ARG(prev_instance);
    UNUSED_ARG(command_line);
    UNUSED_ARG(show_command);

    if (!ParseCommandLine(&argv))
    {   // bail out if the command line cannot be parsed successfully.
        DebugPrintf(_T("ERROR: Unable to parse the command line.\n"));
        return 0;
    }
    if (argv.CreateConsole)
    {   // create the console before doing anything else, so all debug output can be seen.
        CreateConsoleAndRedirectStdio();
    }
    if (!ElevateProcessPrivileges())
    {   // if the necessary privileges could not be obtained, there's no point in proceeding.
        goto cleanup_and_shutdown;
    }

cleanup_and_shutdown:
    if (argv.CreateConsole)
    {
        printf("\nApplication terminated. Press any key to exit.\n");
        (void) _getch();
    }
    // TODO(rlk): perform additional cleanup here.
    return 0;
}

