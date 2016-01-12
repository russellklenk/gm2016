/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define the data associated with user input state for keyboards, 
/// mice and gamepads. Up to four of each device are supported. Data is safe 
/// for access from a single thread only.
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
/// @summary Define the value indicating an unused device handle. Gamepads don't specify a device handle, so they present as (HANDLE) dwPlayerIndex.
#ifndef WIN32_INPUT_DEVICE_HANDLE_NONE
#define WIN32_INPUT_DEVICE_HANDLE_NONE    INVALID_HANDLE_VALUE
#endif

/// @summary Define the value indicating an unused player index. Valid player indices are [0, 3].
#ifndef WIN32_PLAYER_INDEX_NONE
#define WIN32_PLAYER_INDEX_NONE           0xFFFFFFFFUL
#endif

/// @summary Define the value indicating that an input packet was dropped because too many devices of the specified type are attached.
#ifndef WIN32_INPUT_DEVICE_TOO_MANY
#define WIN32_INPUT_DEVICE_TOO_MANY       ~size_t(0)
#endif

/// @summary Define the value indicating that a device was not found in the specified device list.
#ifndef WIN32_INPUT_DEVICE_NOT_FOUND
#define WIN32_INPUT_DEVICE_NOT_FOUND      ~size_t(0)
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
/// @summary Define the data associated with keyboard state.
struct WIN32_KEYBOARD_STATE
{
    uint32_t KeyState[8];                   /// A bitvector (256 bits) mapping scan code to key state (1 = key down.)
};

/// @summary Define a macro for easy static initialization of keyboard state data.
#define WIN32_KEYBOARD_STATE_STATIC_INIT                                       \
    {                                                                          \
        { 0, 0, 0, 0, 0, 0, 0, 0 } /* KeyState */                              \
    }

/// @summary Define the data associated with gamepad state (Xbox controller.)
struct WIN32_GAMEPAD_STATE
{
    uint32_t LTrigger;                      /// The left trigger value, in [0, 255].
    uint32_t RTrigger;                      /// The right trigger value, in [0, 255].
    int32_t  LStick[2];                     /// The left analog stick X and Y values, after deadzone logic is applied.
    int32_t  RStick[2];                     /// The right analog stick X and Y values, after deadzone logic is applied.
    uint32_t Buttons;                       /// A bitvector storing up to 32 button states (1 = button down.)
};

/// @summary Define a macro for easy static initialization of gamepad state data.
#define WIN32_GAMEPAD_STATE_STATIC_INIT                                        \
    {                                                                          \
        0,      /* LTrigger */                                                 \
        0,      /* RTrigger */                                                 \
      { 0, 0 }, /* LStick[X,Y] */                                              \
      { 0, 0 }, /* RStick[X,Y] */                                              \
        0       /* Buttons */                                                  \
    }

/// @summary Define the data associated with a pointing device (like a mouse.)
struct WIN32_POINTER_STATE
{
    int32_t  Pointer[2];                    /// The absolute X and Y coordinates of the pointer, in virtual display space.
    int32_t  Relative[3];                   /// The high definition relative X, Y and Z (wheel) values of the pointer.
    int32_t  Buttons;                       /// A bitvector storing up to 32 button states (0 = left, 1 = right, 2 = middle) (1 = button down.)
};

// @summary Define a macro for easy static initialization of pointer state data.
#define WIN32_POINTER_STATE_STATIC_INIT                                        \
    {                                                                          \
      { 0, 0 }   , /* Pointer[X,Y] */                                          \
      { 0, 0, 0 }, /* Relative[X,Y,Z] */                                       \
        0          /* Buttons */                                               \
    }

/// @summary Define the data associated with a list of user input devices of the same type.
template <typename T>
struct WIN32_INPUT_DEVICE_LIST
{   static size_t const MAX_DEVICES = 4;    /// The maximum number of attached devices.
    size_t DeviceCount;                     /// The number of attached devices.
    HANDLE DeviceHandle[MAX_DEVICES];       /// The OS device handle for each device.
    DWORD  PlayerIndex [MAX_DEVICES];       /// The player index assigned to each device.
    T      DeviceState [MAX_DEVICES];       /// The current state for each device.
};
typedef WIN32_INPUT_DEVICE_LIST<WIN32_KEYBOARD_STATE> WIN32_KEYBOARD_LIST;
typedef WIN32_INPUT_DEVICE_LIST<WIN32_GAMEPAD_STATE>  WIN32_GAMEPAD_LIST;
typedef WIN32_INPUT_DEVICE_LIST<WIN32_POINTER_STATE>  WIN32_POINTER_LIST;

/// @summary Define a macro for easy static initialization of a keyboard list.
#define WIN32_KEYBOARD_LIST_STATIC_INIT                                        \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE                                         \
      },   /* DeviceHandle */                                                  \
      { WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE                                                \
      },   /* PlayerIndex */                                                   \
      { WIN32_KEYBOARD_STATE_STATIC_INIT,                                      \
        WIN32_KEYBOARD_STATE_STATIC_INIT,                                      \
        WIN32_KEYBOARD_STATE_STATIC_INIT,                                      \
        WIN32_KEYBOARD_STATE_STATIC_INIT                                       \
      }    /* DeviceState */                                                   \
    }

/// @summary Define a macro for easy static initialzation of a gamepad list.
#define WIN32_GAMEPAD_LIST_STATIC_INIT                                         \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE                                         \
      },   /* DeviceHandle */                                                  \
      { WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE                                                \
      },   /* PlayerIndex */                                                   \
      { WIN32_GAMEPAD_STATE_STATIC_INIT,                                       \
        WIN32_GAMEPAD_STATE_STATIC_INIT,                                       \
        WIN32_GAMEPAD_STATE_STATIC_INIT,                                       \
        WIN32_GAMEPAD_STATE_STATIC_INIT                                        \
      }    /* DeviceState */                                                   \
    }

/// @summary Define a macro for easy static initialization of a pointer (mouse) list.
#define WIN32_POINTER_LIST_STATIC_INIT                                         \
    {                                                                          \
        0, /* DeviceCount */                                                   \
      { WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE,                                        \
        WIN32_INPUT_DEVICE_HANDLE_NONE                                         \
      },   /* DeviceHandle */                                                  \
      { WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE,                                               \
        WIN32_PLAYER_INDEX_NONE                                                \
      },   /* PlayerIndex */                                                   \
      { WIN32_POINTER_STATE_STATIC_INIT,                                       \
        WIN32_POINTER_STATE_STATIC_INIT,                                       \
        WIN32_POINTER_STATE_STATIC_INIT,                                       \
        WIN32_POINTER_STATE_STATIC_INIT                                        \
      }    /* DeviceState */                                                   \
    }

/// @summary Define the data associated with all input devices attached to the system.
struct WIN32_INPUT_DEVICE_STATE
{   static size_t const MAX_DEVICES = 4;    /// The maximum number of input devices of each type.
    WIN32_KEYBOARD_LIST Keyboards;          /// The list of keyboards attached to the system.
    WIN32_GAMEPAD_LIST  Gamepads;           /// The list of gamepads attached to the system.
    WIN32_POINTER_LIST  Pointers;           /// The list of pointer devices attached to the system.
};

/// @summary Define a macro for easy static initialization of device state data.
#define WIN32_INPUT_DEVICE_STATE_STATIC_INIT                                   \
    {                                                                          \
        WIN32_KEYBOARD_LIST_STATIC_INIT, /* Keyboards */                       \
        WIN32_GAMEPAD_LIST_STATIC_INIT,  /* Gamepads */                        \
        WIN32_POINTER_LIST_STATIC_INIT   /* Pointers */                        \
    }

/*///////////////
//   Globals   //
///////////////*/
/// @summary Define storage for two buffers of input device state. These buffers are swapped on every update.
global_variable WIN32_INPUT_DEVICE_STATE  InputDeviceStateBuffers[2] = 
{
    WIN32_INPUT_DEVICE_STATE_STATIC_INIT, 
    WIN32_INPUT_DEVICE_STATE_STATIC_INIT
};

/// @summary A pointer to the input device state for the previous update tick. Read-only.
global_variable WIN32_INPUT_DEVICE_STATE *PreviousInputBuffer = &InputDeviceStateBuffers[0];

/// @summary A pointer to the input device state for the current update tick. Read-Write.
global_variable WIN32_INPUT_DEVICE_STATE *CurrentInputBuffer  = &InputDeviceStateBuffers[1];

/*///////////////////////
//   Local Functions   //
///////////////////////*/
/// @summary Search a device list for a device with a given handle.
/// @param device_list The device list to search.
/// @param device_handle The handle of the device to locate.
/// @return The zero-based index of the device in the device list, or WIN32_INPUT_DEVICE_NOT_FOUND.
template <typename T>
internal_function inline size_t 
FindInputDeviceForHandle
(
    WIN32_INPUT_DEVICE_LIST<T>  *device_list, 
    HANDLE                     device_handle
)
{
    for (size_t i = 0, n = device_list->DeviceCount; i < n; ++i)
    {
        if (device_list->DeviceHandle[i] == device_handle)
            return i;
    }
    return WIN32_INPUT_DEVICE_NOT_FOUND;
}

/// @summary Search a device list for a device with a given player ID.
/// @param device_list The device list to search.
/// @param player_index The ID of the player to locate.
/// @return The zero-based index of the device in the device list, or WIN32_INPUT_DEVICE_NOT_FOUND.
template <typename T>
internal_function inline size_t
FindInputDeviceForPlayer
(
    WIN32_INPUT_DEVICE_LIST<T> *device_list,
    uint32_t                   player_index
)
{
    for (size_t i = 0, n = device_list->DeviceCount; i < n; ++i)
    {
        if (device_list->PlayerIndex[i] == player_index)
            return i;
    }
    return WIN32_INPUT_DEVICE_NOT_FOUND;
}

/// @summary Process a Raw Input keyboard packet to extract the virtual key code, scan code, and flags.
/// @param input The Raw Input keyboard packet to process.
/// @param virt_key On return, stores the processed virtual key code.
/// @param scan_code On return, stores the processed keyboard scan code.
/// @param flags On return, stores the RI_KEY_* flags associated with the input.
/// @return The zero-based index of the input device in the current input buffer, or WIN32_INPUT_DEVICE_TOO_MANY.
internal_function size_t
ProcessKeyboardPacket
(
    RAWINPUT const     *input,
    uint32_t        &virt_key, 
    uint32_t       &scan_code,
    uint32_t           &flags
)
{
    RAWINPUTHEADER const  &header = input->header;
    RAWKEYBOARD    const     &key = input->data.keyboard;
    WIN32_KEYBOARD_STATE   *state = NULL;
    WIN32_KEYBOARD_LIST  &devices = CurrentInputBuffer->Keyboards;
    size_t                  index = 0;
    uint32_t                 vkey = key.VKey;
    uint32_t                 scan = key.MakeCode;
    bool                       e0 =((key.Flags & RI_KEY_E0) != 0);

    // locate the keyboard in the current state buffer by device handle.
    for (size_t i = 0, n = devices.DeviceCount; i < n; ++i)
    {
        if (devices.DeviceHandle[i] == header.hDevice)
        {   // found the matching device.
            index = i;
            state =&devices.DeviceState[i];
        }
    }
    if (state == NULL)
    {   // this device was newly attached.
        if (devices.DeviceCount == WIN32_KEYBOARD_LIST::MAX_DEVICES)
        {   // there are too many devices of the specified type attached.
            virt_key = vkey; scan_code = scan; flags = key.Flags;
            return WIN32_INPUT_DEVICE_TOO_MANY;
        }
        index = devices.DeviceCount++;
        state =&devices.DeviceState[index];
    }
    if (vkey == 255)
    {   // discard fake keys; these are just part of an escaped sequence.
        virt_key = vkey; scan_code = scan; flags = key.Flags;
        return index;
    }
    if (vkey == VK_SHIFT)
    {   // correct left/right shift.
        vkey  = MapVirtualKey(scan, MAPVK_VSC_TO_VK_EX);
    }
    if (vkey == VK_NUMLOCK)
    {   // correct PAUSE/BREAK and NUMLOCK. set the extended bit.
        scan  = MapVirtualKey(vkey, MAPVK_VK_TO_VSC) | 0x100;
    }
    if (key.Flags & RI_KEY_E1)
    {   // for escaped sequences, turn the virtual key into the correct scan code.
        // unfortunately, MapVirtualKey can't handle VK_PAUSE, so do that manually.
        if (vkey != VK_PAUSE) scan  = MapVirtualKey(vkey, MAPVK_VK_TO_VSC);
        else scan = 0x45;
    }
    switch (vkey)
    {   // map left/right versions of various keys.
        case VK_CONTROL:  /* left/right CTRL */
            vkey =  e0 ? VK_RCONTROL : VK_LCONTROL;
            break;
        case VK_MENU:     /* left/right ALT  */
            vkey =  e0 ? VK_RMENU : VK_LMENU;
            break;
        case VK_RETURN:   /* numpad enter has e0 set */
            vkey =  e0 ? VK_RETURN : VK_RETURN;
            break;
        case VK_INSERT:
            vkey = !e0 ? VK_NUMPAD0 : VK_INSERT;
            break;
        case VK_DELETE:
            vkey = !e0 ? VK_DECIMAL : VK_DELETE;
            break;
        case VK_HOME:
            vkey = !e0 ? VK_NUMPAD7 : VK_HOME;
            break;
        case VK_END:
            vkey = !e0 ? VK_NUMPAD1 : VK_END;
            break;
        case VK_PRIOR:
            vkey = !e0 ? VK_NUMPAD9 : VK_PRIOR;
            break;
        case VK_NEXT:
            vkey = !e0 ? VK_NUMPAD3 : VK_NEXT;
            break;
        case VK_LEFT:
            vkey = !e0 ? VK_NUMPAD4 : VK_LEFT;
            break;
        case VK_RIGHT:
            vkey = !e0 ? VK_NUMPAD6 : VK_RIGHT;
            break;
        case VK_UP:
            vkey = !e0 ? VK_NUMPAD8 : VK_UP;
            break;
        case VK_DOWN:
            vkey = !e0 ? VK_NUMPAD2 : VK_DOWN;
            break;
        case VK_CLEAR:
            vkey = !e0 ? VK_NUMPAD5 : VK_CLEAR;
            break;
    }

    // all custom processing is finished, return the processed data to the caller.
    virt_key  = vkey;
    scan_code = scan;
    flags     = key.Flags;
    return index;
}

/// @summary Tests the flags from a Raw Input keyboard packet to determine if a key was previously pressed and was just released.
/// @param ri_flags The RAWKEYBOARD::Flags field from the input packet.
/// @return true if the key was previously not pressed.
internal_function inline bool
KeyJustReleased
(
    uint32_t ri_flags
)
{
    return ((ri_flags & RI_KEY_BREAK) != 0);
}

/// @summary Tests the flags from a Raw Input keyboard packet to determine if a key is currently pressed.
/// @param ri_flags The RAWKEYBOARD::Flags field from the input packet.
/// @return true if the key is currently pressed.
internal_function inline bool
KeyPressed
(
    uint32_t ri_flags
)
{
    return ((ri_flags & RI_KEY_BREAK) == 0);
}

/// @summary Retrieve the localized display name for a keyboard scan code.
/// @param scan_code The key scan code, as returned by ProcessKeyboardPacket.
/// @param ri_flags The RAWKEYBOARD::Flags field from the input packet.
/// @param buffer The caller-managed buffer into which the display name will be copied.
/// @param buffer_size The maximum number of characters that can be written to the buffer.
/// @return The number of characters written to the buffer, not including the zero codepoint, or 0.
internal_function size_t
CopyKeyDisplayName
(
    uint32_t   scan_code, 
    uint32_t    ri_flags, 
    TCHAR        *buffer, 
    size_t   buffer_size
)
{
    LONG key_code = (scan_code << 16) | (((ri_flags & RI_KEY_E0) ? 1 : 0) << 24);
    return (size_t)  GetKeyNameText(key_code, buffer, (int) buffer_size);
}

internal_function void
ProcessMousePacket
(
    RAWINPUT const *input
)
{
    RAWINPUTHEADER const &header = input->header;
    RAWMOUSE       const &mouse  = input->data.mouse;
    UNUSED_LOCAL(header);
    UNUSED_LOCAL(mouse);
}

internal_function void
ProcessGamepadPacket
(
    XINPUT_STATE const       *input, 
    DWORD              player_index
)
{
    UNUSED_ARG(input);
    UNUSED_ARG(player_index);
}

