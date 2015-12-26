/*/////////////////////////////////////////////////////////////////////////////
/// @summary Implement the entry point of the application.
///////////////////////////////////////////////////////////////////////////80*/

/*////////////////
//   Includes   //
////////////////*/
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>

#include <tchar.h>
#include <windows.h>
#include <shellapi.h>

#include "platform_config.h"
#include "compiler_config.h"

/*////////////////////////
//   Public Functions   //
////////////////////////*/
/// @summary Implements the entry point of the application.
/// @param this_instance The module base address of the executable
/// @param prev_instance Unused. This value is always NULL.
/// @param command_line The zero-terminated command line string.
/// @param show_command One of SW_MAXIMUMIZED, etc. indicating how the main window should be displayed.
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
    UNUSED_ARG(this_instance);
    UNUSED_ARG(prev_instance);
    UNUSED_ARG(command_line);
    UNUSED_ARG(show_command);
    return 0;
}
