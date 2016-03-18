/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define Win32 function prototypes and the functions to resolve them
/// for anything that needs to be 'linked' at runtime, as well as any privilege
/// escalation functions required.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////////
//   Preprocessor   //
////////////////////*/
/// @summary Macro used to declare a function resolved at runtime.
#ifndef DECLARE_WIN32_RUNTIME_FUNCTION
#define DECLARE_WIN32_RUNTIME_FUNCTION(retval, callconv, name, ...) \
    typedef retval (callconv *name##_Fn)(__VA_ARGS__);              \
    extern name##_Fn name##_Func
#endif

/// @summary Macro used to define a function resolved at runtime.
#ifndef DEFINE_WIN32_RUNTIME_FUNCTION
#define DEFINE_WIN32_RUNTIME_FUNCTION(name) \
    /* global_variable */ name##_Fn name##_Func = NULL
#endif

/// @summary Resolve a function pointer from a DLL at runtime. If the function is not available, set the function pointer to a stub function.
/// Some naming conventions must be followed for these macros to work. Given function name:
/// name = SomeFunction
/// The function pointer typedef should be        : SomeFunction_Fn
/// The global function pointer instance should be: SomeFunction_Func
/// The stub function should be                   : SomeFunction_Stub
/// The resolve call should be                    : RESOLVE_WIN32_RUNTIME_FUNCTION(dll_instance, SomeFunction)
#ifndef RESOLVE_WIN32_RUNTIME_FUNCTION
    #define RESOLVE_WIN32_RUNTIME_FUNCTION(dll, fname)                         \
        do {                                                                   \
            fname##_Func = (fname##_Fn) GetProcAddress(dll, #fname);           \
            if (fname##_Func == NULL) { fname##_Func = fname##_Stub; }         \
        __pragma(warning(push));                                               \
        __pragma(warning(disable:4127));                                       \
        } while (0);                                                           \
        __pragma(warning(pop))
#endif

/// @summary Determine whether a Win32 runtime function was resolved to its stub implementation.
#ifndef WIN32_RUNTIME_STUB
    #define WIN32_RUNTIME_STUB(name) \
        name##_Func == name##_Stub
#endif

/// @summary The GetFinalPathNameByHandle function has two potential entry points, one for Unicode and one for the ANSI character set.
#ifndef GetFinalPathNameByHandle_Func
    #ifdef  _UNICODE
        #define GetFinalPathNameByHandle_Func    GetFinalPathNameByHandleW_Func
    #else
        #define GetFinalPathNameByHandle_Func    GetFinalPathNameByHandleA_Func
    #endif
#endif

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Functions in NtDll.dll use NTAPI (which resolves to __stdcall).
/// Define this token so that the build doesn't depend on having WinDDK available.
#ifndef NTAPI
    #define NTAPI                                       __stdcall
#endif

/// @summary Functions in NtDll.dll use NTSTATUS as a common return type. 
/// See https://msdn.microsoft.com/en-us/library/windows/hardware/ff565436(v=vs.85).aspx
/// See https://msdn.microsoft.com/en-us/library/cc704588.aspx
#ifndef NTSTATUS
    #define NTSTATUS                                    LONG
#endif

/// @summary The NTSTATUS code meaning that an operation was successful.
#ifndef STATUS_SUCCESS
    #define STATUS_SUCCESS                              ((NTSTATUS) 0x00000000L)
#endif

/// @summary The NTSTATUS code meaning that a function is not implemented. Used in stub functions.
#ifndef STATUS_NOT_IMPLEMENTED
    #define STATUS_NOT_IMPLEMENTED                      ((NTSTATUS) 0xC0000002L)
#endif

/// @summary Check an NTSTATUS value to determine if the operation was successful.
#ifndef NT_SUCCESS
    #define NT_SUCCESS(_status)                         ((NTSTATUS)(_status) >= 0)
#endif

/// @summary Check an NTSTATUS value to determine if the operation has the informational bit set.
#ifndef NT_INFORMATION
    #define NT_INFORMATION(_status)                     ((((ULONG) (_status)) >> 30) == 1)
#endif

/// @summary Check an NTSTATUS value to determine if the operation has the warning bit set.
#ifndef NT_WARNING
    #define NT_WARNING(_status)                         ((((ULONG) (_status)) >> 30) == 2)
#endif

/// @summary Check an NTSTATUS value to determine if the operation has the error bit set.
#ifndef NT_ERROR
    #define NT_ERROR(_status)                           ((((ULONG) (_status)) >> 30) == 3)
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the supported runtime users. Certain functionality, like XInput, is not loaded on the server.
enum WIN32_RUNTIME_TYPE : int
{
    WIN32_RUNTIME_TYPE_CLIENT = 1,    /// The Win32 runtime is being loaded on the client.
    WIN32_RUNTIME_TYPE_SERVER = 2,    /// The Win32 runtime is being loaded on the server.
};

/// @summary Define the CPU topology information reported to the application.
struct WIN32_CPU_INFO
{
    size_t NumaNodes;                 /// The number of NUMA nodes in the system.
    size_t PhysicalCPUs;              /// The number of physical CPUs installed in the system.
    size_t PhysicalCores;             /// The total number of physical cores in all CPUs.
    size_t HardwareThreads;           /// The total number of hardware threads in all CPUs.
    size_t ThreadsPerCore;            /// The number of hardware threads per physical core.
    char   VendorName[13];            /// The CPUID vendor string.
    char   PreferAMD;                 /// Set to 1 if AMD OpenCL implementations are preferred.
    char   PreferIntel;               /// Set to 1 if Intel OpenCL implementations are preferred.
    char   IsVirtualMachine;          /// Set to 1 if the process is running in a virtual machine.
};

DECLARE_WIN32_RUNTIME_FUNCTION(void    , WINAPI, XInputEnable                      , BOOL);                                                   // XInput1_4.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, XInputGetState                    , DWORD, XINPUT_STATE*);                                   // XInput1_4.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, XInputSetState                    , DWORD, XINPUT_VIBRATION*);                               // XInput1_4.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, XInputGetCapabilities             , DWORD, DWORD, XINPUT_CAPABILITIES*);                     // XInput1_4.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, XInputGetBatteryInformation       , DWORD, BYTE , XINPUT_BATTERY_INFORMATION*);              // XInput1_4.dll
DECLARE_WIN32_RUNTIME_FUNCTION(void    , WINAPI, GetNativeSystemInfo               , SYSTEM_INFO*);                                           // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(BOOL    , WINAPI, SetProcessWorkingSetSizeEx        , HANDLE , SIZE_T, SIZE_T, DWORD);                         // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(BOOL    , WINAPI, SetFileInformationByHandle        , HANDLE , FILE_INFO_BY_HANDLE_CLASS, LPVOID, DWORD);      // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(BOOL    , WINAPI, GetQueuedCompletionStatusEx       , HANDLE , OVERLAPPED_ENTRY*, ULONG, ULONG*, DWORD, BOOL); // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(BOOL    , WINAPI, SetFileCompletionNotificationModes, HANDLE , UCHAR);                                         // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, GetFinalPathNameByHandleA         , HANDLE , LPSTR , DWORD, DWORD);                          // Kernel32.dll
DECLARE_WIN32_RUNTIME_FUNCTION(DWORD   , WINAPI, GetFinalPathNameByHandleW         , HANDLE , LPWSTR, DWORD, DWORD);                          // Kernel32.dll

/*///////////////
//   Globals   //
///////////////*/
DEFINE_WIN32_RUNTIME_FUNCTION(XInputEnable);
DEFINE_WIN32_RUNTIME_FUNCTION(XInputGetState);
DEFINE_WIN32_RUNTIME_FUNCTION(XInputSetState);
DEFINE_WIN32_RUNTIME_FUNCTION(XInputGetCapabilities);
DEFINE_WIN32_RUNTIME_FUNCTION(XInputGetBatteryInformation);
DEFINE_WIN32_RUNTIME_FUNCTION(GetNativeSystemInfo);
DEFINE_WIN32_RUNTIME_FUNCTION(GetFinalPathNameByHandleA);
DEFINE_WIN32_RUNTIME_FUNCTION(GetFinalPathNameByHandleW);
DEFINE_WIN32_RUNTIME_FUNCTION(SetProcessWorkingSetSizeEx);
DEFINE_WIN32_RUNTIME_FUNCTION(SetFileInformationByHandle);
DEFINE_WIN32_RUNTIME_FUNCTION(GetQueuedCompletionStatusEx);
DEFINE_WIN32_RUNTIME_FUNCTION(SetFileCompletionNotificationModes);

/// @summary The module load address of the XInput DLL.
global_variable HMODULE XInputDll   = NULL;

/// @summary The module load address of Kernel32.dll.
global_variable HMODULE Kernel32Dll = NULL;

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary No-op stub function for XInputEnable.
/// @param enable If enable is FALSE XInput will only send neutral data in response to XInputGetState.
internal_function void WINAPI
XInputEnable_Stub
(
    BOOL enable
)
{
    UNUSED_ARG(enable);
}

/// @summary No-op stub function for XInputGetState.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param pState Pointer to an XINPUT_STATE structure that receives the current state of the controller.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputGetState_Stub
(
    DWORD        dwUserIndex, 
    XINPUT_STATE     *pState
)
{
    UNUSED_ARG(dwUserIndex);
    UNUSED_ARG(pState);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputSetState.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param pVibration Pointer to an XINPUT_VIBRATION structure containing vibration information to send to the controller.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputSetState_Stub
(
    DWORD            dwUserIndex, 
    XINPUT_VIBRATION *pVibration
)
{
    UNUSED_ARG(dwUserIndex);
    UNUSED_ARG(pVibration);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputGetCapabilities.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param dwFlags Flags that identify the controller type, either 0 or XINPUT_FLAG_GAMEPAD.
/// @param pCapabilities Pointer to an XINPUT_CAPABILITIES structure that receives the controller capabilities.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputGetCapabilities_Stub
(
    DWORD                  dwUserIndex, 
    DWORD                      dwFlags, 
    XINPUT_CAPABILITIES *pCapabilities
)
{
    UNUSED_ARG(dwUserIndex);
    UNUSED_ARG(dwFlags);
    UNUSED_ARG(pCapabilities);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary No-op stub function for XInputGetBatteryInformation.
/// @param dwUserIndex The index of the user's controller, in [0, 3].
/// @param devType Specifies which device associated with this user index should be queried. One of BATTERY_DEVTYPE_GAMEPAD or BATTERY_DEVTYPE_HEADSET.
/// @param Pointer to an XINPUT_BATTERY_INFORMATION structure that receives the battery information.
/// @return ERROR_SUCCESS or ERROR_DEVICE_NOT_CONNECTED.
internal_function DWORD WINAPI
XInputGetBatteryInformation_Stub
(
    DWORD                               dwUserIndex, 
    BYTE                                    devType, 
    XINPUT_BATTERY_INFORMATION *pBatteryInformation
)
{
    UNUSED_ARG(dwUserIndex);
    UNUSED_ARG(devType);
    UNUSED_ARG(pBatteryInformation);
    return ERROR_DEVICE_NOT_CONNECTED;
}

/// @summary Retrievss information about the current system to an application running under WOW64.
/// @param system_info Pointer to a SYSTEM_INFO structure that receives the information.
internal_function void WINAPI
GetNativeSystemInfo_Stub
(
    SYSTEM_INFO *system_info
)
{
    UNUSED_ARG(system_info);
}

/// @summary Sets the minimum and maximum workins set sizes for the specified process.
/// @param process A handle to the process whose working set size is to be set.
/// @param min_size The minimum working set size for the process, in bytes.
/// @param max_size The maximum working set size for the process, in bytes.
/// @param flags Flags that control the enforcement of the minimum and maximum working set sizes.
/// @return Non-zero if successful, or zero if an error occurred.
internal_function BOOL WINAPI
SetProcessWorkingSetSizeEx_Stub
(
    HANDLE  process,  
    SIZE_T min_size, 
    SIZE_T max_size, 
    DWORD     flags
)
{
    UNUSED_ARG(process);
    UNUSED_ARG(min_size);
    UNUSED_ARG(max_size);
    UNUSED_ARG(flags);
    return FALSE;
}

/// @summary Sets the file information for the specified file.
/// @param file A handle to the file for which information will be changed.
/// @param file_information_class A FILE_INFO_BY_HANDLE_CLASS enumeration that specifies the type of information to set.
/// @param file_information_data Pointer to a buffer specifying the information to change.
/// @param file_information_size The size of the buffer pointed to by file_information_data.
/// @return Non-zero if successful, or zero if an error occurred.
internal_function BOOL WINAPI
SetFileInformationByHandle_Stub
(
    HANDLE                                      file, 
    FILE_INFO_BY_HANDLE_CLASS file_information_class, 
    LPVOID                     file_information_data, 
    DWORD                      file_information_size
)
{
    UNUSED_ARG(file);
    UNUSED_ARG(file_information_class);
    UNUSED_ARG(file_information_data);
    UNUSED_ARG(file_information_size);
    return FALSE;
}

/// @summary Retrieves the status of multiple completion port entries simultaneously.
/// @param completion_port The handle of the completion port.
/// @param completion_port_entries Points to a pre-allocated array of OVERLAPPED_ENTRY to populate.
/// @param count The maximum number of entries to remove.
/// @param num_entries_removed On return, specifies the number of entries actually removed.
/// @param timeout The maximum number of milliseconds that the caller is willing to wait for a completion port entry.
/// @param alertable Specify TRUE to perform an alertable wait.
/// @return Non-zero if successful, or zero if an error occurred.
internal_function BOOL WINAPI
GetQueuedCompletionStatusEx_Stub
(
    HANDLE                    completion_port,
    OVERLAPPED_ENTRY *completion_port_entries, 
    ULONG                               count, 
    PULONG                num_entries_removed, 
    DWORD                             timeout, 
    BOOL                            alertable
)
{
    UNUSED_ARG(completion_port);
    UNUSED_ARG(completion_port_entries);
    UNUSED_ARG(count);
    UNUSED_ARG(timeout);
    UNUSED_ARG(alertable);
    if (num_entries_removed != NULL) *num_entries_removed = 0;
    return FALSE;
}

/// @summary Sets the notification modes for a file handle.
/// @param file_handle A handle to the file.
/// @param flags The modes to be set.
/// @return Non-zero if successful, or zero if an error occurred.
internal_function BOOL WINAPI
SetFileCompletionNotificationModes_Stub
(
    HANDLE file_handle, 
    UCHAR        flags
)
{
    UNUSED_ARG(file_handle);
    UNUSED_ARG(flags);
    return FALSE;
}

/// @summary Retrieves the final path for the specified file.
/// @param file_handle A handle to a file or directory.
/// @param file_path Pointer to a buffer to receive the path.
/// @param max_file_path_chars The maximum number of characters to write to the file path buffer.
/// @param flags Bitflags specifying how to return the result.
/// @return The length of the path string, or zero if an error occurs.
internal_function DWORD WINAPI
GetFinalPathNameByHandleA_Stub
(
    HANDLE         file_handle, 
    LPSTR            file_path, 
    DWORD  max_file_path_chars,  
    DWORD                flags
)
{
    UNUSED_ARG(file_handle);
    UNUSED_ARG(file_path);
    UNUSED_ARG(max_file_path_chars);
    UNUSED_ARG(flags);
    return 0;
}

/// @summary Retrieves the final path for the specified file.
/// @param file_handle A handle to a file or directory.
/// @param file_path Pointer to a buffer to receive the path.
/// @param max_file_path_chars The maximum number of characters to write to the file path buffer.
/// @param flags Bitflags specifying how to return the result.
/// @return The length of the path string, or zero if an error occurs.
internal_function DWORD WINAPI
GetFinalPathNameByHandleW_Stub
(
    HANDLE         file_handle, 
    LPWSTR           file_path, 
    DWORD  max_file_path_chars,  
    DWORD                flags
)
{
    UNUSED_ARG(file_handle);
    UNUSED_ARG(file_path);
    UNUSED_ARG(max_file_path_chars);
    UNUSED_ARG(flags);
    return 0;
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
        bool se_debug       = EnableProcessPrivilege(token, SE_DEBUG_NAME, TRUE);
        bool se_volume_name = EnableProcessPrivilege(token, SE_MANAGE_VOLUME_NAME, TRUE);
        // ...
        CloseHandle(token);
        return (se_debug && se_volume_name);
    }
    return false;
}

/// @summary Load Kernel32.dll into the process address space and resolve the required API functions.
/// @param missing_entry_points On return, set to true if any entry points are missing.
/// @return true if Kernel32.dll was loaded into the process address space.
internal_function bool
LoadKernel
(
    bool *missing_entry_points
)
{
    HMODULE kernel32_dll = LoadLibrary(_T("kernel32.dll"));
    if (kernel32_dll == NULL)
    {   // something is seriously wrong.
        if (missing_entry_points) *missing_entry_points = true;
        return false;
    }
    
    // perform runtime resolution of all required API functions.
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, GetNativeSystemInfo);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, SetProcessWorkingSetSizeEx);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, SetFileInformationByHandle);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, GetQueuedCompletionStatusEx);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, SetFileCompletionNotificationModes);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, GetFinalPathNameByHandleA);
    RESOLVE_WIN32_RUNTIME_FUNCTION(kernel32_dll, GetFinalPathNameByHandleW);

    // check for any entry points that got set to their stub functions:
    if (WIN32_RUNTIME_STUB(GetNativeSystemInfo))                goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(SetProcessWorkingSetSizeEx))         goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(SetFileInformationByHandle))         goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(GetQueuedCompletionStatusEx))        goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(SetFileCompletionNotificationModes)) goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(GetFinalPathNameByHandleA))          goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(GetFinalPathNameByHandleW))          goto missing_entry_point;

    // save the DLL handle.
    Kernel32Dll = kernel32_dll;
    return true;

missing_entry_point:
    if (missing_entry_points) *missing_entry_points = true;
    Kernel32Dll = kernel32_dll;
    return true;
}

/// @summary Load the latest version of XInput into the process address space and resolve the required API functions.
/// @param missing_entry_points On return, set to true if any entry points are missing.
/// @return true if the XInput DLL was loaded into the process address space.
internal_function bool
LoadXInput
(
    bool *missing_entry_points
)
{   // start by trying to load the most recent version of the DLL, which ships with the Windows 8 SDK.
    HMODULE xinput_dll = NULL;
    if ((xinput_dll = LoadLibrary(_T("xinput1_4.dll"))) == NULL)
    {   // try with XInput 9.1.0, which shipped starting with Windows Vista.
        if ((xinput_dll = LoadLibrary(_T("xinput9_1_0.dll"))) == NULL)
        {   // try for XInput 1.3, which shipped in the June 2010 DirectX SDK.
            if ((xinput_dll = LoadLibrary(_T("xinput1_3.dll"))) == NULL)
            {   // no XInput is available, so resolve everything to the stub functions.
                if (missing_entry_points) *missing_entry_points = true;
                return false;
            }
        }
    }

    // perform runtime resolution of all required API functions.
    RESOLVE_WIN32_RUNTIME_FUNCTION(xinput_dll, XInputEnable);
    RESOLVE_WIN32_RUNTIME_FUNCTION(xinput_dll, XInputGetState);
    RESOLVE_WIN32_RUNTIME_FUNCTION(xinput_dll, XInputSetState);
    RESOLVE_WIN32_RUNTIME_FUNCTION(xinput_dll, XInputGetCapabilities);
    RESOLVE_WIN32_RUNTIME_FUNCTION(xinput_dll, XInputGetBatteryInformation);

    // check for any entry points that got set to their stub functions.
    if (WIN32_RUNTIME_STUB(XInputEnable))                goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(XInputGetState))              goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(XInputSetState))              goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(XInputGetCapabilities))       goto missing_entry_point;
    if (WIN32_RUNTIME_STUB(XInputGetBatteryInformation)) goto missing_entry_point;

    // save the DLL handle.
    XInputDll = xinput_dll;
    return true;

missing_entry_point:
    if (missing_entry_points) *missing_entry_points = true;
    XInputDll = xinput_dll;
    return true;
}

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Initialize the runtime environment by loading all required APIs at runtime and requesting any elevated privileges.
/// @param runtime_type One of WIN32_RUNTIME_TYPE specifying whether the runtime is being initialized on the client or the server.
/// @return true if the runtime environment was successfully initialized.
public_function bool 
InitializeRuntime
(
    int runtime_type
)
{ 
    bool missing_entry_points = false;

    // load routines required by both client and server.
    if (!LoadKernel(&missing_entry_points))
    {   // without the kernel routines, there's no point in continuing.
        return false;
    }

    if (runtime_type == WIN32_RUNTIME_TYPE_CLIENT)
    {   // resolve client-only entry points.
        if (!LoadXInput(&missing_entry_points))
        {   // without XInput, there is no support for gamepads.
            return false;
        }
    }
    if (runtime_type == WIN32_RUNTIME_TYPE_SERVER)
    {   // resolve server-only entry points.
    
    }
    return ElevateProcessPrivileges();
}

/// @summary Enumerate all CPU resources of the host system. Allocates temporary memory with malloc/free.
/// @param cpu_info The structure to populate with information about host CPU resources.
/// @return true if the host CPU information was successfully retrieved.
public_function bool
EnumerateHostCPU
(
    WIN32_CPU_INFO *cpu_info
)
{
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *lpibuf = NULL;
    SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX *info   = NULL;
    size_t     smt_count = 0;
    uint8_t     *bufferp = NULL;
    uint8_t     *buffere = NULL;
    DWORD    buffer_size = 0;
    int          regs[4] ={0, 0, 0, 0};

    // zero out the CPU information returned to the caller.
    ZeroMemory(cpu_info, sizeof(WIN32_CPU_INFO));
    
    // retrieve the CPU vendor string using the __cpuid intrinsic.
    __cpuid(regs  , 0); // CPUID function 0
    *((int*)&cpu_info->VendorName[0]) = regs[1]; // EBX
    *((int*)&cpu_info->VendorName[4]) = regs[3]; // ECX
    *((int*)&cpu_info->VendorName[8]) = regs[2]; // EDX
         if (!strcmp(cpu_info->VendorName, "AuthenticAMD")) cpu_info->PreferAMD        = true;
    else if (!strcmp(cpu_info->VendorName, "GenuineIntel")) cpu_info->PreferIntel      = true;
    else if (!strcmp(cpu_info->VendorName, "KVMKVMKVMKVM")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "Microsoft Hv")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "VMwareVMware")) cpu_info->IsVirtualMachine = true;
    else if (!strcmp(cpu_info->VendorName, "XenVMMXenVMM")) cpu_info->IsVirtualMachine = true;

    // figure out the amount of space required, and allocate a temporary buffer:
    GetLogicalProcessorInformationEx(RelationAll, NULL, &buffer_size);
    if ((lpibuf = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) malloc(size_t(buffer_size))) == NULL)
    {   // unable to allocate the required memory:
        cpu_info->NumaNodes       = 1;
        cpu_info->PhysicalCPUs    = 1;
        cpu_info->PhysicalCores   = 1;
        cpu_info->HardwareThreads = 1;
        cpu_info->ThreadsPerCore  = 1;
        return false;
    }
    GetLogicalProcessorInformationEx(RelationAll, lpibuf, &buffer_size);

    // initialize the output counts:
    cpu_info->NumaNodes       = 0;
    cpu_info->PhysicalCPUs    = 0;
    cpu_info->PhysicalCores   = 0;
    cpu_info->HardwareThreads = 0;
    cpu_info->ThreadsPerCore  = 0;

    // step through the buffer and update counts:
    bufferp = (uint8_t*) lpibuf;
    buffere =((uint8_t*) lpibuf) + size_t(buffer_size);
    while (bufferp < buffere)
    {
        info = (SYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX*) bufferp;
        switch (info->Relationship)
        {
            case RelationNumaNode:
                {
                    cpu_info->NumaNodes++;
                } break;

            case RelationProcessorPackage:
                {
                    cpu_info->PhysicalCPUs++;
                } break;

            case RelationProcessorCore:
                {
                    cpu_info->PhysicalCores++;
                    if (info->Processor.Flags == LTP_PC_SMT)
                        smt_count++;
                } break;

            default:
                {   // RelationGroup, RelationCache - don't care.
                } break;
        }
        bufferp += size_t(info->Size);
    }
    // free the temporary buffer:
    free(lpibuf);

    // determine the total number of logical processors in the system.
    // use this value to figure out the number of threads per-core.
    if (smt_count > 0)
    {   // determine the number of logical processors in the system and
        // use this value to figure out the number of threads per-core.
        SYSTEM_INFO sysinfo;
        GetNativeSystemInfo(&sysinfo);
        cpu_info->ThreadsPerCore = size_t(sysinfo.dwNumberOfProcessors) / smt_count;
    }
    else
    {   // there are no SMT-enabled CPUs in the system, so 1 thread per-core.
        cpu_info->ThreadsPerCore = 1;
    }

    // calculate the total number of available hardware threads.
    cpu_info->HardwareThreads = (smt_count * cpu_info->ThreadsPerCore) + (cpu_info->PhysicalCores - smt_count);
    return true;
}

