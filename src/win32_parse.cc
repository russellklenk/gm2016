/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define routines for parsing text data. Some portions of this code
/// work with TCHAR (such as command-line parsing) and so the parsing module 
/// is considered to be platform-dependent.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Compare two command line argument keys for equality. By default, the comparison ignores case differences.
/// @param key The zero-terminated key string, typically returned by ArgumentKeyAndValue.
/// @param match The zero-terminated key string to compare with, typically a string literal.
/// @param ignore_case Specify true to ignore differences in character case.
/// @return true if the string key is equivalent to the string match.
internal_function bool
KeyMatch
(
    TCHAR const        *key, 
    TCHAR const      *match, 
    bool        ignore_case = true
)
{
    if (ignore_case) return _tcsicmp(key, match) == 0;
    else return _tcscmp(key, match) == 0;
}

/// @summary Parse a command-line argument to retrieve the key or key-value pair. Codepoint '-', '--' or '/' are supported as lead characters on the key, and key-value pairs may be specified as 'key=value'.
/// @param arg The input argument string. Any equal codepoint '=' is replaced with a zero codepoint.
/// @param key On return, points to the start of the zero-terminated argument key, or to the zero codepoint.
/// @param val On return, points to the start of the zero-terminated argument value, or to the zero codepoint.
/// @return true if a key or key-value pair was extracted, or false if the input string is NULL or empty.
internal_function bool
ArgumentKeyAndValue
(
    TCHAR  *arg, 
    TCHAR **key, 
    TCHAR **val
)
{   // if the input string is NULL, return false - no key-value pair is returned.
    if (arg == NULL)
        return false;

    // the key and the value both start out pointing to the start of the argument string.
    *key = arg; *val = arg;

    // strip off any leading characters to find the start of the key.
    TCHAR  ch = *arg;
    while (ch)
    {
        if (ch == _T('/') || ch == _T('-') || ch == _T(' '))
        {   // skip the lead character.
           *key = ++arg;
        }
        else if (ch == _T('='))
        {   // replace the key-value separator with a zero codepoint.
           *arg++ = 0;
            break;
        }
        else
        {   // this character is part of the key, advance to the next codepoint.
            // this is the most likely case; we will advance until the end of the
            // string is reached or until the key-value separator is reached.
            arg++;
        }
        ch = *arg;
    }
    // arg points to the start of the value, or to the zero codepoint.
    *val = arg;
    // only return true if the key is not empty.
    return (*key[0] != 0);
}

/// @summary Find the end of a zero-terminated string.
/// @param str The zero-terminated string.
/// @return A pointer to the zero terminator, or NULL if the input string is NULL.
internal_function TCHAR*
StringEnd
(
    TCHAR *str
)
{
    if (str != NULL)
    {
        TCHAR  *iter = str;
        while (*iter)
        {
            ++iter;
        }
        return iter;
    }
    return NULL;
}

/// @summary Determine whether a character represents a decimal digit.
/// @param ch The character to inspect.
/// @return true if the specified character is a decimal digit (0-9).
internal_function bool
IsDigit
(
    TCHAR ch
)
{
    return (ch >= _T('0') && ch <= _T('9'));
}

/// @summary Parse a string representation of an unsigned 32-bit integer.
/// @param first A pointer to the first codepoint to inspect.
/// @param last A pointer to the last codepoint to inspect.
/// @param result On return, stores the integer value.
/// @return A pointer to the first codepoint that is not part of the number, in [first, last].
internal_function TCHAR*
StrToUInt32
(
    TCHAR     *first, 
    TCHAR      *last, 
    uint32_t &result

)
{
    uint32_t num = 0;
    for ( ; first != last && IsDigit(*first); ++first)
    {
        num = 10 * num + (*first - _T('0'));
    }
    result = num;
    return first;
}

