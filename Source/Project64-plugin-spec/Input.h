#pragma once
#include "Base.h"

enum PluginType
{
    PLUGIN_NONE = 1,
    PLUGIN_MEMPAK = 2,
    PLUGIN_RUMBLE_PAK = 3,
    PLUGIN_TRANSFER_PAK = 4,
    PLUGIN_RAW = 5,
};

enum PresentType
{
    PRESENT_NONE = 0,
    PRESENT_CONT = 1,
    PRESENT_MOUSE = 2,
};

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct
{
    int32_t Present;
    int32_t RawData;
    int32_t Plugin;
} CONTROL;

#pragma warning(push)
#pragma warning(disable : 4201) // warning C4201: nonstandard extension used: nameless struct/union

typedef union
{
    uint32_t Value;
    struct
    {
        unsigned R_DPAD : 1;
        unsigned L_DPAD : 1;
        unsigned D_DPAD : 1;
        unsigned U_DPAD : 1;
        unsigned START_BUTTON : 1;
        unsigned Z_TRIG : 1;
        unsigned B_BUTTON : 1;
        unsigned A_BUTTON : 1;

        unsigned R_CBUTTON : 1;
        unsigned L_CBUTTON : 1;
        unsigned D_CBUTTON : 1;
        unsigned U_CBUTTON : 1;
        unsigned R_TRIG : 1;
        unsigned L_TRIG : 1;
        unsigned Reserved1 : 1;
        unsigned Reserved2 : 1;

        signed X_AXIS : 8;

        signed Y_AXIS : 8;
    };
} BUTTONS;

#pragma warning(pop)

typedef struct
{
    void * hWnd;
    void * hinst;
    int32_t Reserved;
    uint8_t * HEADER;         // This is the ROM header (first 40h bytes of the ROM)
    CONTROL * Controls;       // A pointer to an array of 4 controllers
} CONTROL_INFO;

enum KeyboardMouseKey
{
    KeyboardMouseKey_A = 4,
    KeyboardMouseKey_D = 7,
    KeyboardMouseKey_E = 8,
    KeyboardMouseKey_F = 9,
    KeyboardMouseKey_Q = 20,
    KeyboardMouseKey_S = 22,
    KeyboardMouseKey_W = 26,
    KeyboardMouseKey_Z = 29,
    KeyboardMouseKey_Return = 40,
    KeyboardMouseKey_Space = 44,
    KeyboardMouseKey_LeftControl = 224,
    KeyboardMouseKey_LeftShift = 225,
    KeyboardMouseKey_RightControl = 228,
};

enum
{
    KeyboardMouseKeyCount = 512,
    KeyboardMouseButtonCount = 8,
};

typedef struct
{
    uint32_t Size;
    int32_t MouseX;
    int32_t MouseY;
    int32_t MouseWheel;
    uint8_t MouseButtons[KeyboardMouseButtonCount];
    uint8_t Keys[KeyboardMouseKeyCount];
} KEYBOARD_MOUSE_STATE;

/*
Function: ControllerCommand
Purpose: To process the raw data that has just been sent to a
specific controller.
Input: Controller number (0 to 3) and -1 signaling end of
processing the PIF RAM.
- Pointer of data to be processed.
Output: None
Note: This function is only needed if the DLL is allowing raw
data, or the plugin is set to raw.
The data that is being processed looks like this:
Initialize controller: 01 03 00 FF FF FF
Read controller: 01 04 01 FF FF FF FF
*/
EXPORT void CALL ControllerCommand(int32_t Control, uint8_t * Command);

/*
Function: GetKeys
Purpose: To get the current state of the controllers buttons.
Input: Controller number (0 to 3)
- A pointer to a BUTTONS structure to be filled with
the controller state.
Output: None
*/
EXPORT void CALL GetKeys(int32_t Control, BUTTONS * Keys);

/*
Optional Project64 extension used by game-specific input runtimes.
Key indexes are physical USB HID/SDL scancodes and are independent of the
active keyboard layout. Mouse deltas cover the same interval as GetKeys.
*/
EXPORT int32_t CALL GetKeyboardMouseState(KEYBOARD_MOUSE_STATE * State);

/* Optional companion extension controlling relative mouse capture. */
EXPORT void CALL SetKeyboardMouseCapture(int32_t Capture);

/*
Function: InitiateControllers
Purpose: This function initializes how each of the controllers
should be handled.
Input: The handle to the main window.
- A controller structure that needs to be filled for
the emulator to know how to handle each controller.
Output: None
*/
EXPORT void CALL InitiateControllers(CONTROL_INFO * ControlInfo);

/*
Function: ReadController
Purpose: To process the raw data in the PIF RAM that is about to
be read.
Input: Controller number (0 to 3) and -1 signaling end of
processing the PIF RAM.
- Pointer of data to be processed.
Output: None
Note: This function is only needed if the DLL is allowing raw
data.
*/
EXPORT void CALL ReadController(int Control, uint8_t * Command);


/*
Function: WM_KeyDown
Purpose: To pass the WM_KeyDown message from the emulator to the
plugin.
Input: wParam and lParam of the WM_KEYDOWN message.
Output: None
*/
EXPORT void CALL WM_KeyDown(uint32_t wParam, uint32_t lParam);

/*
Function: WM_KeyUp
Purpose: To pass the WM_KEYUP message from the emulator to the
plugin.
Input: wParam and lParam of the WM_KEYDOWN message.
Output: None
*/
EXPORT void CALL WM_KeyUp(uint32_t wParam, uint32_t lParam);

/*
Function: WM_KillFocus
Purpose: To pass the WM_KILLFOCUS message from the emulator to the
plugin.
Input: wParam and lParam of the WM_KILLFOCUS message.
Output: None
*/
EXPORT void CALL WM_KillFocus(uint32_t wParam, uint32_t lParam);

#if defined(__cplusplus)
}
#endif
