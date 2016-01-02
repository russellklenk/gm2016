/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define Win32 function prototypes and the functions to resolve them
/// for anything that needs to be 'linked' at runtime, as well as any privilege
/// escalation functions required.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
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

/*////////////////////////
//   Public Functions   //
////////////////////////*/
public_function bool 
Win32InitializeRuntime
(
    void
)
{   // TODO(rlk): Resolve any Vista+ functions you think are necessary.
    return ElevateProcessPrivileges();
}

