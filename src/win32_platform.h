/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define Win32 platform structures shared across platform modules.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Defines the data representing the result of parsing command line arguments.
struct WIN32_COMMAND_LINE
{
    bool                CreateConsole;   /// If true, attach a console for viewing debug output.
};

/// @summary Define arguments passed to all explicit threads by the main thread.
struct WIN32_THREAD_ARGS
{
    HANDLE              StartEvent;      /// All threads wait on this manual-reset event before entering their main body.
    HANDLE              TerminateEvent;  /// All threads poll this manual-reset event. If signaled, they terminate.
    HINSTANCE           ModuleBaseAddr;  /// The module load address of the executable.
    WIN32_COMMAND_LINE *CommandLine;     /// Parsed command-line argument information.
};

/// @summary Define the function signature for a thread entry point.
/// @param argp A pointer to a WIN32_THREAD_ARGS strycture.
typedef void (__stdcall *WIN32_THREAD_ENTRYPOINT)(void *argp);

