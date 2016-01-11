/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the body of the primary rendering coordination thread. 
/// The rendering coordination thread is responsible for the management of the
/// main output window, handling resource creation and management, and frame 
/// composition. The rendering thread runs at a fixed rate.
///////////////////////////////////////////////////////////////////////////80*/

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define various flags specifying window states or attributes.
enum   WINDOW_FLAGS : uint32_t
{
    WINDOW_FLAGS_NONE       = (0UL << 0UL),    /// No flags are set on the window.
    WINDOW_FLAGS_WINDOWED   = (1UL << 0UL),    /// The window is currently presented in a windowed style.
    WINDOW_FLAGS_FULLSCREEN = (1UL << 1UL),    /// The window is currently presented in a fullscreen style.
};

/// @summary Stores the data associated with a display output attached to the system.
struct WIN32_DISPLAY
{
    DWORD           Ordinal;                   /// The unique display ordinal.
    HMONITOR        Monitor;                   /// The operating system monitor identifier.
    int             DisplayX;                  /// The x-coordinate of the upper-left corner of the display, in virtual screen space.
    int             DisplayY;                  /// The y-coordinate of the upper-left corner of the display, in virtual screen space.
    int             DisplayWidth;              /// The width of the display, in pixels.
    int             DisplayHeight;             /// The height of the display, in pixels.
    DEVMODE         DisplayMode;               /// The active display settings.
    DISPLAY_DEVICE  DisplayInfo;               /// Information uniquely identifying the display to the operating system.
};

/// @summary Stores data associated with a window.
struct WIN32_WINDOW
{
    HWND            Window;                    /// The handle of the window.
    WINDOWPLACEMENT Placement;                 /// The placement of the window on the display.
    uint32_t        Flags;                     /// Current state and attribute flags.
};

/*///////////////
//   Globals   //
///////////////*/
/// @summary Define the maximum number of attached displays recognized by the application.
global_variable size_t const  MAX_ATTACHED_DISPLAYS                    = 32;

/// @summary Define storage for a list of attributes of all attached displays.
global_variable WIN32_DISPLAY GlobalDisplayList[MAX_ATTACHED_DISPLAYS] = {};

/// @summary Define the number of valid records in the list of attached displays.
global_variable size_t        GlobalDisplayCount                       =  0;

/*//////////////////////////
//   Internal Functions   //
//////////////////////////*/
/// @summary Enumerate and retrieve information for all displays attached to the system.
/// @param display_list The caller-managed array of display information to populate.
/// @param max_displays The maximum number of display records that can be written to display_list.
/// @param num_displays On return, stores the number of display records written to display_list.
internal_function void
EnumerateAttachedDisplays
(
    WIN32_DISPLAY *display_list, 
    size_t const   max_displays, 
    size_t        &num_displays
)
{
    DISPLAY_DEVICE  dd ={}; dd.cb = sizeof(DISPLAY_DEVICE);
    for (DWORD ordinal = 0; EnumDisplayDevices(NULL, ordinal, &dd, 0) && num_displays < max_displays; ++ordinal)
    {   // ignore pseudo-displays and displays that aren't attached to a desktop.
        if ((dd.StateFlags & DISPLAY_DEVICE_MIRRORING_DRIVER) != 0)
            continue;
        if ((dd.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) == 0)
            continue;

        // retrieve display orientation and geometry.
        DEVMODE dm = {};
        dm.dmSize  = sizeof(DEVMODE);
        if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_CURRENT_SETTINGS, &dm, 0))
        {   // try for the registry settings instead.
            if (!EnumDisplaySettingsEx(dd.DeviceName, ENUM_REGISTRY_SETTINGS, &dm, 0))
            {   // unable to retrieve the current display settings. skip the display.
                continue;
            }
        }
        
        // make sure that the display size and position were returned.
        if ((dm.dmFields & DM_POSITION) == 0 || 
            (dm.dmFields & DM_PELSWIDTH) == 0 || 
            (dm.dmFields & DM_PELSHEIGHT) == 0)
        {   // position and size are required. skip the display.
            continue;
        }

        // build a RECT representing the monitor bounds, used to retrieve the monitor handle.
        RECT monitor_bounds = {
            (LONG) dm.dmPosition.x,                         // left
            (LONG) dm.dmPosition.y,                         // top
            (LONG) dm.dmPosition.x + (LONG) dm.dmPelsWidth, // right
            (LONG) dm.dmPosition.y + (LONG) dm.dmPelsHeight // bottom
        };

        // fill out the display record:
        WIN32_DISPLAY  *display = &display_list[num_displays++];
        display->Ordinal        =  ordinal;
        display->Monitor        =  MonitorFromRect(&monitor_bounds, MONITOR_DEFAULTTONEAREST);
        display->DisplayX       =  dm.dmPosition.x;
        display->DisplayY       =  dm.dmPosition.y;
        display->DisplayWidth   =  dm.dmPelsWidth;
        display->DisplayHeight  =  dm.dmPelsHeight;
        CopyMemory(&display->DisplayMode, &dm, (SIZE_T) dm.dmSize);
        CopyMemory(&display->DisplayInfo, &dd, (SIZE_T) dd.cb);
    }
}

/// @summary Helper function to enumerate all displays attached to the system. Display information is stored in the global list.
/// @return true if display resources are initialized and at least one display is attached to the system.
internal_function bool
EnumerateAttachedDisplays
(
    void
)
{
    GlobalDisplayCount = 0;
    EnumerateAttachedDisplays(GlobalDisplayList, MAX_ATTACHED_DISPLAYS, GlobalDisplayCount);
    return (GlobalDisplayCount > 0);
}

/// @summary Locate the display containing a given window.
/// @param display_list The list of attached displays to search.
/// @param num_displays The number of records in the display_list.
/// @param window The handle of the window to search for.
/// @return A pointer to the display record containing most of the window content, or NULL if no display was found. If a display was added or removed, it may be necessary to re-enumerate the set of attached displays.
internal_function WIN32_DISPLAY*
FindDisplayForWindow
(
    WIN32_DISPLAY *display_list, 
    size_t const   num_displays, 
    HWND                 window
)
{
    HMONITOR monitor = MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST);
    for (size_t i = 0; i < num_displays; ++i)
    {
        if (display_list[i].Monitor == monitor)
            return &display_list[i];
    }
    // it's possible that a display was added or removed, so in this case it may
    // be necessary to re-enumerate the set of displays attached to the system.
    return NULL;
}

/// @summary Locate the primary display attached to the system.
/// @param display_list The list of attached displays to search.
/// @param num_displays The number of records in the display_list.
/// @return A pointer to the record associated with the primary display, or NULL if no displays are attached.
internal_function WIN32_DISPLAY*
FindPrimaryDisplay
(
    WIN32_DISPLAY *display_list, 
    size_t const   num_displays
)
{
    if (num_displays == 0)
    {   // no displays are attached to the system.
        return NULL;
    }

    // consider display 0 to be the primary if none is explicitly marked as such.
    WIN32_DISPLAY *primary_display = &display_list[0];
    for (size_t i = 0; i < num_displays; ++i)
    {
        if (display_list[i].DisplayInfo.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE)
        {
            primary_display = &display_list[i];
            break;
        }
    }
    return primary_display;
}

/// @summary Locate the primary display attached to the system.
/// @return A pointer to the record associated with the primary display, or NULL if no displays are attached.
internal_function inline WIN32_DISPLAY*
FindPrimaryDisplay
(
    void
)
{
    return FindPrimaryDisplay(GlobalDisplayList, GlobalDisplayCount);
}

/// @summary Retrieve the current refresh rate, in Hz, for a given display or window.
/// @param display The display record for the display containing the given window.
/// @param window The window associated with the given display.
/// @return The display refresh rate, in Hz.
internal_function int
DisplayRefreshRate
(
    WIN32_DISPLAY *display, 
    HWND            window
)
{   // have a sane default in case the refresh rate cannot be determined.
    int const DEFAULT_REFRESH_RATE = 60;

    if (display == NULL)
    {   // default to a rate of 60Hz.
        return DEFAULT_REFRESH_RATE;
    }

    if (display->DisplayMode.dmDisplayFrequency == 0 || 
        display->DisplayMode.dmDisplayFrequency == 1)
    {   // a value of 0 or 1 indicates the 'default' refresh rate.
        if (window != NULL)
        {   // query the refresh rate through the device context.
            HDC dc = GetDC(window);
            int hz = GetDeviceCaps(dc, VREFRESH);
            ReleaseDC(window, dc);
            return hz;
        }
        else
        {   // no window was specified, so just guess.
            return DEFAULT_REFRESH_RATE;
        }
    }
    else
    {   // the DEVMODE specifies the current refresh rate.
        return display->DisplayMode.dmDisplayFrequency;
    }
}

/// @summary Retrieve the name of a display.
/// @param display The display information to query.
/// @return A zero-terminated string specifying the system display name.
internal_function inline TCHAR*
DisplayName
(
    WIN32_DISPLAY *display
)
{
    return display->DisplayInfo.DeviceName;
}

/// @summary Implements the WndProc for the main game window.
/// @param window The window receiving the message.
/// @param message The message identifier.
/// @param wparam Additional message-specific data.
/// @param lparam Additional message-specific data.
/// @return The message-specific result code.
internal_function LRESULT CALLBACK
MainWindowCallback
(
    HWND   window, 
    UINT   message, 
    WPARAM wparam, 
    LPARAM lparam
)
{
    LRESULT result = 0;
    switch (message)
    {
        case WM_ACTIVATEAPP:
            {   // wparam is TRUE if the window is being activated, or FALSE if
                // the window is being deactivated. 
            } break;

        case WM_CLOSE:
            {   // completely destroy the main window. a WM_DESTROY message is 
                // posted that in turn causes the WM_QUIT message to be posted.
                DestroyWindow(window);
            } break;

        case WM_DESTROY:
            {   // post the WM_QUIT message to be picked up in the main loop.
                PostQuitMessage(0);
            } break;

        case WM_DISPLAYCHANGE:
            {   // re-enumerate all attached displays. a display may have been added or removed, 
                // and likely the geometry of all attached displays was affected in some way.
                EnumerateAttachedDisplays();
            } break;

        default:
            {   // pass the message to the default handler.
                result = DefWindowProc(window, message, wparam, lparam);
            } break;
    }
    return result;
}

/// @summary Create a new window on a given display.
/// @param window The window definition to initialize.
/// @param this_instance The HINSTANCE of the application (passed to WinMain) or GetModuleHandle(NULL).
/// @param display The target display, or NULL to use the primary display.
/// @param width The width of the window, or 0 to use the entire width of the display.
/// @param height The height of the window, or 0 to use the entire height of the display.
/// @param fullscreen Specify true to create a fullscreen-styled window.
/// @return The handle of the new window, or NULL.
internal_function bool
CreateWindowOnDisplay
(
    WIN32_WINDOW        *window,
    HINSTANCE     this_instance,
    WIN32_DISPLAY      *display, 
    int                   width, 
    int                  height, 
    bool             fullscreen
)
{
    TCHAR const *class_name = _T("GM2016_WndClass");
    WNDCLASSEX     wndclass = {};

    if (display == NULL)
    {   // create the window on the primary display.
        if ((display = FindPrimaryDisplay(GlobalDisplayList, GlobalDisplayCount)) == NULL)
        {   // no displays are attached to the system, so fail.
            return NULL;
        }
    }

    // register the window class, if necessary.
    if (!GetClassInfoEx(this_instance, class_name, &wndclass))
    {   // the window class hasn't been registered yet.
        wndclass.cbSize         = sizeof(WNDCLASSEX);
        wndclass.cbClsExtra     = 0;
        wndclass.cbWndExtra     = sizeof(void*);
        wndclass.hInstance      = this_instance;
        wndclass.lpszClassName  = class_name;
        wndclass.lpszMenuName   = NULL;
        wndclass.lpfnWndProc    = MainWindowCallback;
        wndclass.hIcon          = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hIconSm        = LoadIcon  (0, IDI_APPLICATION);
        wndclass.hCursor        = LoadCursor(0, IDC_ARROW);
        wndclass.style          = CS_OWNDC | CS_HREDRAW | CS_VREDRAW;
        wndclass.hbrBackground  = NULL;
        if (!RegisterClassEx(&wndclass))
        {   // unable to register the window class, cannot proceed.
            return false;
        }
    }

    // clamp the bounds to the width and height of the display.
    if (fullscreen || (width == 0 && height == 0))
    {   // use the entire dimensions of the display.
        width      = display->DisplayWidth;
        height     = display->DisplayHeight;
        fullscreen = true;
    }
    if (width  == 0 || width  > display->DisplayWidth)
        width   = display->DisplayWidth;
    if (height == 0 || height > display->DisplayHeight)
        height  = display->DisplayHeight;

    // create a new window (not yet visible) at location (0, 0) on the display.
    int   x        = display->DisplayX;
    int   y        = display->DisplayY;
    DWORD style_ex = 0;
    DWORD style    =(fullscreen ? 0 : WS_OVERLAPPEDWINDOW) | WS_CLIPCHILDREN | WS_CLIPSIBLINGS;
    TCHAR*title    = display->DisplayInfo.DeviceName;
    RECT  client   = {};
    HWND  hwnd     = CreateWindowEx(style_ex, class_name, title, style, x, y, width, height, NULL, NULL, this_instance, NULL);
    if   (hwnd  == NULL)
    {   // the window cannot be created.
        return false;
    }

    // center the window on the display if running in windowed mode.
    GetClientRect(hwnd, &client);
    if (AdjustWindowRectEx(&client, style & ~WS_OVERLAPPED, FALSE, style_ex))
    {   // account for any scroll bars, which are not included in AdjustWindowRectEx.
        if (style & WS_VSCROLL) client.right  += GetSystemMetrics(SM_CXVSCROLL);
        if (style & WS_HSCROLL) client.bottom += GetSystemMetrics(SM_CYHSCROLL);
        // move the window so that it is perfectly centered on the display.
        int client_w = client.right - client.left;
        int client_h = client.bottom - client.top;
        int client_x =(display->DisplayWidth  / 2) - (client_w / 2);
        int client_y =(display->DisplayHeight / 2) - (client_h / 2);
        SetWindowPos(hwnd, HWND_TOP, client_x, client_y, client_w, client_h, 0);
    }

    // finally, display the window and return the handle.
    window->Window = hwnd;
    window->Placement.length = sizeof(WINDOWPLACEMENT);
    GetWindowPlacement(hwnd, &window->Placement);

    window->Flags = WINDOW_FLAGS_NONE;
    if (fullscreen) window->Flags |= WINDOW_FLAGS_FULLSCREEN;
    else window->Flags |= WINDOW_FLAGS_WINDOWED;

    ShowWindow(hwnd, SW_SHOWDEFAULT);
    return true;
}

/// @summary Toggles a window between fullscreen and windowed mode styles.
/// @param window The window to toggle.
internal_function void
ToggleFullscreen
(
    WIN32_WINDOW *window
)
{
    HWND      hwnd = window->Window;
    LONG_PTR style = GetWindowLongPtr(hwnd, GWL_STYLE);
    if (style & WS_OVERLAPPEDWINDOW)
    {   // switch to a fullscreen style window.
        MONITORINFO monitor_info = { sizeof(MONITORINFO) };
        WINDOWPLACEMENT win_info = { sizeof(WINDOWPLACEMENT) };
        if (GetWindowPlacement(hwnd, &win_info) && GetMonitorInfo(MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor_info))
        {   // modify the window style, size and placement.
            RECT rc = monitor_info.rcMonitor;
            CopyMemory(&window->Placement, &win_info, (SIZE_T) win_info.length);
            SetWindowLongPtr(hwnd, GWL_STYLE, style &~ WS_OVERLAPPEDWINDOW);
            SetWindowPos(hwnd, HWND_TOP, rc.left, rc.top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
            window->Flags &= ~WINDOW_FLAGS_WINDOWED;
            window->Flags |=  WINDOW_FLAGS_FULLSCREEN;
        }
    }
    else
    {   // switch to a windowed style window.
        SetWindowLongPtr(hwnd, GWL_STYLE, style | WS_OVERLAPPEDWINDOW);
        SetWindowPlacement(hwnd, &window->Placement);
        SetWindowPos(hwnd, 0, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_FRAMECHANGED); 
        window->Flags &= ~WINDOW_FLAGS_FULLSCREEN;
        window->Flags |=  WINDOW_FLAGS_FULLSCREEN;
    }
}

/// @summary Determine if a window is currently being displayed with a fullscreen style.
/// @param window The window to check.
/// @return true if the window is displayed in a fullscreen style.
internal_function inline bool
IsFullscreen
(
    WIN32_WINDOW *window
)
{
    return (window->Flags & WINDOW_FLAGS_FULLSCREEN) == WINDOW_FLAGS_FULLSCREEN;
}

/// @summary Determine if a window is currently being displayed with a windowed style.
/// @param window The window to check.
/// @return true if the window is displayed in a windowed style.
internal_function inline bool
IsWindowed
(
    WIN32_WINDOW *window
)
{
    return (window->Flags & WINDOW_FLAGS_WINDOWED) == WINDOW_FLAGS_WINDOWED;
}


/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Implement the entry point for the main frame composition thread.
/// @param args Pointer to a WIN32_THREAD_ARGS structure populated by the main thread.
public_function unsigned int __cdecl
RenderThread
(
    void *args
)
{
    WIN32_WINDOW       main_window  = {};
    WIN32_DISPLAY     *main_display = NULL;
    WIN32_THREAD_ARGS *thread_args  = (WIN32_THREAD_ARGS*) args;
    bool               keep_running = true;

    if (!EnumerateAttachedDisplays())
    {
        ConsoleError("ERROR: No displays are attached to the system; render thread cannot start.\n");
        goto terminate_thread;
    }
    if ((main_display = FindPrimaryDisplay()) == NULL)
    {
        ConsoleError("ERROR: Unable to find the primary display.\n");
        goto terminate_thread;
    }
    if (WaitForSingleObject(thread_args->StartEvent, INFINITE) != WAIT_OBJECT_0)
    {
        ConsoleError("ERROR: Render thread failed while waiting on launch signal.\n");
        goto terminate_thread;
    }

    // create the main application window on the primary display.
    if (!CreateWindowOnDisplay(&main_window, thread_args->ModuleBaseAddr, main_display, 800, 600, false))
    {   // unable to create the main application window; there's no point in proceeding.
        ConsoleError("ERROR: Unable to create the main window on display %s.\n", DisplayName(main_display));
        goto terminate_thread;
    }

    // run the main thread loop.
    while (keep_running)
    {   
        MSG msg;

        // check for externally-signaled thread termination.
        if (WaitForSingleObject(thread_args->TerminateEvent, 0) == WAIT_OBJECT_0)
        {   // early termination was signaled by an outside thread.
            keep_running = false;
        }

        // dispatch windows messages while messages are available.
        while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
        {
            if (msg.message != WM_QUIT)
            {   // dispatch the message to MainWindowCallback.
                TranslateMessage(&msg);
                DispatchMessage (&msg);
            }
            else
            {   // terminate the main loop because the window was closed.
                keep_running = false;
                break;
            }
        }

        if (!keep_running)
        {   // the render thread has been instructed to terminate.
            break;
        }

        // TODO(rlk): Present the backbuffer

    }

    ConsoleError("STATUS: The render thread is exiting.\n");
    SetEvent(thread_args->TerminateEvent);
    return 0;

terminate_thread:
    SetEvent(thread_args->TerminateEvent);
    return 1;
}

