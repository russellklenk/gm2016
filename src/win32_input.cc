/*/////////////////////////////////////////////////////////////////////////////
/// @summary Define the data associated with user input state for keyboards, 
/// mice and gamepads. Up to four of each device are supported. Data is safe 
/// for access from a single thread only.
///////////////////////////////////////////////////////////////////////////80*/

/*/////////////////
//   Constants   //
/////////////////*/
#ifndef WIN32_INPUT_DEVICE_HANDLE_NONE
#define WIN32_INPUT_DEVICE_HANDLE_NONE    INVALID_HANDLE_VALUE
#endif

#ifndef WIN32_PLAYER_INDEX_NONE
#define WIN32_PLAYER_INDEX_NONE           0xFFFFFFFFUL
#endif

/*//////////////////
//   Data Types   //
//////////////////*/
struct WIN32_KEYBOARD_STATE
{
    uint32_t KeyState[8];
};
#define WIN32_KEYBOARD_STATE_STATIC_INIT                                       \
    {                                                                          \
        { 0, 0, 0, 0, 0, 0, 0, 0 } /* KeyState */                              \
    }

struct WIN32_GAMEPAD_STATE
{
    uint32_t LTrigger;
    uint32_t RTrigger;
    int32_t  LStick[2];
    int32_t  RStick[2];
    uint32_t Buttons;
};
#define WIN32_GAMEPAD_STATE_STATIC_INIT                                        \
    {                                                                          \
        0,      /* LTrigger */                                                 \
        0,      /* RTrigger */                                                 \
      { 0, 0 }, /* LStick[X,Y] */                                              \
      { 0, 0 }, /* RStick[X,Y] */                                              \
        0       /* Buttons */                                                  \
    }

struct WIN32_POINTER_STATE
{
    int32_t  Pointer[2];
    int32_t  Buttons;
};
#define WIN32_POINTER_STATE_STATIC_INIT                                        \
    {                                                                          \
      { 0, 0 }, /* Pointer[X,Y] */                                             \
        0       /* Buttons */                                                  \
    }

template <typename T>
struct WIN32_INPUT_DEVICE_LIST
{   static size_t const MAX_DEVICES = 4;
    size_t DeviceCount;
    HANDLE DeviceHandle[MAX_DEVICES];
    DWORD  PlayerIndex [MAX_DEVICES];
    T      DeviceState [MAX_DEVICES];
};
typedef WIN32_INPUT_DEVICE_LIST<WIN32_KEYBOARD_STATE> WIN32_KEYBOARD_LIST;
typedef WIN32_INPUT_DEVICE_LIST<WIN32_GAMEPAD_STATE>  WIN32_GAMEPAD_LIST;
typedef WIN32_INPUT_DEVICE_LIST<WIN32_POINTER_STATE>  WIN32_POINTER_LIST;
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

struct WIN32_INPUT_DEVICE_STATE
{   static size_t const MAX_DEVICES = 4;
    WIN32_KEYBOARD_LIST Keyboards;
    WIN32_GAMEPAD_LIST  Gamepads;
    WIN32_POINTER_LIST  Pointers;
};
#define WIN32_INPUT_DEVICE_STATE_STATIC_INIT                                   \
    {                                                                          \
        WIN32_KEYBOARD_LIST_STATIC_INIT, /* Keyboards */                       \
        WIN32_GAMEPAD_LIST_STATIC_INIT,  /* Gamepads */                        \
        WIN32_POINTER_LIST_STATIC_INIT   /* Pointers */                        \
    }

/*///////////////
//   Globals   //
///////////////*/
global_variable WIN32_INPUT_DEVICE_STATE  InputDeviceStateBuffers[2] = 
{
    WIN32_INPUT_DEVICE_STATE_STATIC_INIT, 
    WIN32_INPUT_DEVICE_STATE_STATIC_INIT
};

global_variable WIN32_INPUT_DEVICE_STATE *PreviousInputBuffer        = &InputDeviceStateBuffers[0];

global_variable WIN32_INPUT_DEVICE_STATE *CurrentInputBuffer         = &InputDeviceStateBuffers[1];

/*///////////////////////
//   Local Functions   //
///////////////////////*/

