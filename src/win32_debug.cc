/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define platform-specific debugging routines.
///////////////////////////////////////////////////////////////////////////80*/

/*///////////////
//   Globals   //
///////////////*/
/// @summary Define the maximum number of saved indentation levels.
#ifndef MAX_SAVED_INDENT_LEVELS
#define MAX_SAVED_INDENT_LEVELS   64
#endif

/// @summary The current output indentation level.
global_variable __declspec(thread) int  GlobalIndent = 0;

/// @summary The zero-based index specifying the current top-of-stack for saved indent levels.
global_variable __declspec(thread) int  GlobalIndentTOS = 0;

/// @summary A stack used to save and restore the global indentation level.
global_variable __declspec(thread) int  GlobalIndentStack[MAX_SAVED_INDENT_LEVELS] = {};

/// @summary Helper macro to write a formatted string to stderr. The output will not be visible unless a console window is opened.
#define ConsoleError(formatstr, ...) \
    _ftprintf(stderr, _T("%*s") _T(formatstr), (GlobalIndent*2), _T(""), __VA_ARGS__)

/// @summary Helper macro to write a formatting string to stdout. The output will not be visible unless a console window is opened.
#define ConsoleOutput(formatstr, ...) \
    _ftprintf(stdout, _T("%*s") _T(formatstr), (GlobalIndent*2), _T(""), __VA_ARGS__)

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Provides scoped modification of the indentation level for the calling thread.
struct INDENT_SCOPE
{
    size_t Marker;               /// The marker returned by SaveConsoleIndent.
    INDENT_SCOPE(int indent=-1); /// Save the current indentation level and possibly apply a new level.
   ~INDENT_SCOPE(void);          /// Restore the indentation level at scope entry.
    void   Set(int indent);      /// Save the current indentation level and apply a new indentation level.
};

/// @summary Define the data used to specify the name of a thread. Copied straight from MSDN.
struct WIN32_THREAD_NAME_INFO
{
    DWORD  Type;                 /// Must be 0x1000.
    LPCSTR Name;                 /// Pointer to the thread name in user address space.
    DWORD  ThreadId;             /// The thread identifier, or -1 to indicate the calling thread.
    DWORD  Flags;                /// Reserved for future reference, set to 0.
};

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Retrieves the current indentation level.
/// @return The current output indentation level for the calling thread.
public_function int
GetConsoleIndent
(
    void
)
{
    return GlobalIndent;
}

/// @summary Sets the current indentation level for the calling thread, without saving the existing value.
/// @param indent The new indentation level.
public_function void
SetConsoleIndent
(
    int indent
)
{
    if (indent >= 0)
    {   // only allow indentation levels >= 0.
        GlobalIndent = indent;
    }
}

/// @summary Saves the current indentation level for the calling thread, and applies a new indentation level.
/// @param new_indent The new indentation level, or -1 to keep the current indentation level.
/// @return The indentation stack marker representing the current level of indentation.
public_function size_t
SaveConsoleIndent
(
    int new_indent = -1
)
{
    if (GlobalIndentTOS <  MAX_SAVED_INDENT_LEVELS)
    {
        size_t marker = GlobalIndentTOS;
        GlobalIndentStack[GlobalIndentTOS++] = GlobalIndent;
        SetConsoleIndent(new_indent);
        return marker;
    }
    return MAX_SAVED_INDENT_LEVELS-1;
}

/// @summary Restores the indentation level for the calling thread to the current value at the time of the last SaveIndent call.
public_function void
RestoreConsoleIndent
(
    void
)
{
    if (GlobalIndentTOS >= 0)
    {   // restore the saved indentation level.
        int saved_indent = GlobalIndentStack[GlobalIndentTOS];
        SetConsoleIndent(saved_indent);
    }
    if (GlobalIndentTOS >  0)
    {   // pop from the stack.
        GlobalIndentTOS--;
    }
}

/// @summary Restores the indentation level for the calling thread to the value at the specified SaveIndent marker.
/// @param marker The value returned by a previous call to SaveIndent.
public_function void
RestoreConsoleIndent
(
    size_t marker
)
{
    int saved_indent = -1;
    while (GlobalIndentTOS >= marker)
    {
        saved_indent =  GlobalIndentStack[GlobalIndentTOS];
        if (marker > 0) GlobalIndentTOS--;
    }
    SetConsoleIndent(saved_indent);
}

/// @summary Ignore the indentation level for the current thread. The current indentation level is saved; use RestoreIndent to restore it.
public_function void
IgnoreConsoleIndent
(
    void
)
{
    SaveConsoleIndent(0);
}

/// @summary Increase the current indentation level for the calling thread by 1.
public_function void
IncreaseConsoleIndent
(
    void
)
{
    SetConsoleIndent(GetConsoleIndent()+1);
}

/// @summary Decrease the current indentation level for the calling thread by 1.
public_function void
DecreaseConsoleIndent
(
    void
)
{
    SetConsoleIndent(GetConsoleIndent()-1);
}

/// @summary Output a message to the debugger output stream using a printf-style format string.
/// @param format The format string. See https://msdn.microsoft.com/en-us/library/56e442dc.aspx
public_function void
DebugPrintf
(
    TCHAR const *format, 
    ...
)
{
    size_t const BUFFER_SIZE_CHARS = 2048;
    TCHAR    fmt_buffer[BUFFER_SIZE_CHARS];
    int      str_chars = 0;
    va_list  arg_list;
    va_start(arg_list, format);
    if ((str_chars = _vsntprintf_s(fmt_buffer, BUFFER_SIZE_CHARS, _TRUNCATE, format, arg_list)) >= 0)
    {   // send the formatted output to the debugger channel.
        OutputDebugString(fmt_buffer);
    }
    else
    {   // TODO(rlk): make it easier to debug failure.
        OutputDebugString(_T("ERROR: DebugPrintf invalid arguments or buffer too small...\n"));
    }
    va_end(arg_list);
}

/// @summary Set the name of a thread for easier identification in the debugger.
/// @param thread_id The system identifier of the thread, or -1 to set the name of the calling thread.
/// @param thread_name Pointer to a zero-terminated ASCII string speifying the thread name.
public_function void
SetThreadName
(
    DWORD    thread_id, 
    LPCSTR thread_name
)
{
    WIN32_THREAD_NAME_INFO info = {
        0x1000, 
        thread_name, 
        thread_id, 
        0
    };

    __try
    {
        RaiseException(0x406D1388, 0, sizeof(info) / sizeof(DWORD), (ULONG_PTR const *) &info);
    }
    __except (EXCEPTION_CONTINUE_EXECUTION)
    {
        /* empty */
    }
}

/// @summary Save the current indentation level and possibly apply a new indentation level.
/// @param indent The indentation level to apply, or -1 to leave the current level unchanged.
inline INDENT_SCOPE::INDENT_SCOPE(int indent /*=-1*/)
{
    Marker = SaveConsoleIndent(indent);
}

/// @summary Restore the saved indentation level when the object goes out of scope.
inline INDENT_SCOPE::~INDENT_SCOPE(void)
{
    RestoreConsoleIndent(Marker);
}

/// @summary Save the current indentation level and possibly apply a new indentation level.
/// @param indent The indentation level to apply, or -1 to leave the current level unchanged.
inline void INDENT_SCOPE::Set(int indent)
{
    Marker = SaveConsoleIndent(indent);
}

