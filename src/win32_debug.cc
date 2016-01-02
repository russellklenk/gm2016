/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define platform-specific debugging routines.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
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

