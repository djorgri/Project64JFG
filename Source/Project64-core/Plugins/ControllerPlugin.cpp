#include "stdafx.h"

#include "ControllerPlugin.h"
#include <Project64-core/N64System/GameHacks/JetForceGemini.h>
#include <Project64-core/N64System/Mips/Register.h>
#include <Project64-core/N64System/N64Rom.h>
#include <Project64-core/N64System/N64System.h>
#include <Project64-core/N64System/SystemGlobals.h>

CControl_Plugin::CControl_Plugin(void) :
    WM_KeyDown(nullptr),
    WM_KeyUp(nullptr),
    RumbleCommand(nullptr),
    GetKeys(nullptr),
    ReadController(nullptr),
    ControllerCommand(nullptr),
    m_AllocatedControllers(false),
    m_GetKeyboardMouseState(nullptr),
    m_SetKeyboardMouseCapture(nullptr),
    m_GetGamepadState(nullptr),
    m_JfgRuntime(nullptr),
    m_GameInputCaptured(false)
{
    memset(&m_PluginControllers, 0, sizeof(m_PluginControllers));
    memset(&m_Controllers, 0, sizeof(m_Controllers));
    memset(&m_JfgInput, 0, sizeof(m_JfgInput));
    memset(&m_PluginPresent, 0, sizeof(m_PluginPresent));
    memset(&m_JfgForcedPresent, 0, sizeof(m_JfgForcedPresent));
}

CControl_Plugin::~CControl_Plugin()
{
    Close(nullptr);
    UnloadPlugin();
}

bool CControl_Plugin::LoadFunctions(void)
{
    // Find entries for functions in DLL
    void(CALL * InitiateControllers)(void);
    LoadFunction(InitiateControllers);
    LoadFunction(ControllerCommand);
    LoadFunction(GetKeys);
    LoadFunction(ReadController);
    LoadFunction(WM_KeyDown);
    LoadFunction(WM_KeyUp);
    LoadFunction(RumbleCommand);
    LoadFunction(WM_KillFocus);
    _LoadFunction("GetKeyboardMouseState", m_GetKeyboardMouseState);
    _LoadFunction("SetKeyboardMouseCapture", m_SetKeyboardMouseCapture);
    _LoadFunction("GetGamepadState", m_GetGamepadState);

    // Make sure DLL had all needed functions
    if (InitiateControllers == nullptr)
    {
        UnloadPlugin();
        return false;
    }

    if (m_PluginInfo.Version >= 0x0102)
    {
        if (PluginOpened == nullptr)
        {
            UnloadPlugin();
            return false;
        }
    }

    // Allocate our own controller
    m_AllocatedControllers = true;
    for (int32_t i = 0; i < 4; i++)
    {
        m_Controllers[i] = new CCONTROL(m_PluginControllers[i].Present, m_PluginControllers[i].RawData, m_PluginControllers[i].Plugin);
    }
    return true;
}

bool CControl_Plugin::Initiate(CN64System * System, RenderWindow * Window)
{
    static uint8_t Buffer[100];

    SetGameInputCapture(false);
    delete m_JfgRuntime;
    m_JfgRuntime = nullptr;

    for (int32_t i = 0; i < 4; i++)
    {
        m_PluginControllers[i].Present = PRESENT_NONE;
        m_PluginControllers[i].RawData = false;
        m_PluginControllers[i].Plugin = PLUGIN_NONE;
    }

    // Test plugin version
    if (m_PluginInfo.Version == 0x0100)
    {
        // Get function from DLL
        void(CALL * InitiateControllers_1_0)(void * hMainWindow, CONTROL Controls[4]);
        _LoadFunction("InitiateControllers", InitiateControllers_1_0);
        if (InitiateControllers_1_0 == nullptr)
        {
            return false;
        }
#ifdef _WIN32
        InitiateControllers_1_0(Window->GetWindowHandle(), m_PluginControllers);
#else
        InitiateControllers_1_0(nullptr, m_PluginControllers);
#endif
        m_Initialized = true;
    }
    else if (m_PluginInfo.Version >= 0x0101)
    {
        CONTROL_INFO ControlInfo;
        ControlInfo.Controls = m_PluginControllers;
        ControlInfo.HEADER = (System == nullptr ? Buffer : g_Rom->GetRomAddress());
#ifdef _WIN32
        ControlInfo.hinst = Window ? Window->GetModuleInstance() : nullptr;
        ControlInfo.hWnd = Window ? Window->GetWindowHandle() : nullptr;
#else
        ControlInfo.hinst = nullptr;
        ControlInfo.hWnd = nullptr;
#endif
        ControlInfo.Reserved = true;

        if (m_PluginInfo.Version == 0x0101)
        {
            // Get function from DLL
            void(CALL * InitiateControllers_1_1)(CONTROL_INFO ControlInfo);
            _LoadFunction("InitiateControllers", InitiateControllers_1_1);
            if (InitiateControllers_1_1 == nullptr)
            {
                return false;
            }

            InitiateControllers_1_1(ControlInfo);
            m_Initialized = true;
        }
        else if (m_PluginInfo.Version >= 0x0102)
        {
            // Get function from DLL
            void(CALL * InitiateControllers_1_2)(CONTROL_INFO * ControlInfo);
            _LoadFunction("InitiateControllers", InitiateControllers_1_2);
            if (InitiateControllers_1_2 == nullptr)
            {
                return false;
            }

            InitiateControllers_1_2(&ControlInfo);
            m_Initialized = true;
        }
    }
    // Remember what the plugin plugged in before any JFG source overrides it
    for (int32_t i = 0; i < 4; i++)
    {
        m_PluginPresent[i] = m_PluginControllers[i].Present;
        m_JfgForcedPresent[i] = false;
    }
    memset(&m_JfgInput, 0, sizeof(m_JfgInput));
    if (m_Initialized && System != nullptr && !System->m_SyncSystem)
    {
        m_JfgRuntime = new CJetForceGeminiRuntime(System->m_MMU_VM, System->m_Recomp);
        const bool SyncJfgAudio = m_JfgRuntime->SupportsCurrentRom() &&
                                  g_Settings->LoadBool(Setting_JfgSyncAudio);
        g_Settings->SaveBool(Setting_SyncViaAudioEnabled, SyncJfgAudio);
        System->RefreshSyncToAudio();
    }
    return m_Initialized;
}

void CControl_Plugin::UnloadPluginDetails(void)
{
    SetGameInputCapture(false);
    if (m_AllocatedControllers)
    {
        for (int32_t count = 0; count < sizeof(m_Controllers) / sizeof(m_Controllers[0]); count++)
        {
            delete m_Controllers[count];
            m_Controllers[count] = nullptr;
        }
    }

    m_AllocatedControllers = false;
    delete m_JfgRuntime;
    m_JfgRuntime = nullptr;
    ControllerCommand = nullptr;
    GetKeys = nullptr;
    m_GetKeyboardMouseState = nullptr;
    m_SetKeyboardMouseCapture = nullptr;
    m_GetGamepadState = nullptr;
    ReadController = nullptr;
    WM_KeyDown = nullptr;
    WM_KeyUp = nullptr;
}

void CControl_Plugin::UpdateKeys(void)
{
    if (!m_AllocatedControllers)
    {
        return;
    }
    for (int32_t cont = 0; cont < sizeof(m_Controllers) / sizeof(m_Controllers[0]); cont++)
    {
        if (!m_Controllers[cont]->Present())
        {
            continue;
        }
        if (!m_Controllers[cont]->m_RawData)
        {
            GetControllerState(cont, &m_Controllers[cont]->m_Buttons);
        }
        else
        {
            g_Notify->BreakPoint(__FILE__, __LINE__);
        }
    }
    // The Project64 input plugin refreshes its raw keyboard/mouse snapshot
    // from this callback.  JFG still needs that snapshot, but its mapped N64
    // buttons are discarded in GetControllerState when exclusive input is on.
    if (ReadController)
    {
        ReadController(-1, nullptr);
    }
}

void CControl_Plugin::GetControllerState(int32_t Control, BUTTONS * Keys)
{
    if (Keys == nullptr)
    {
        return;
    }

    // UpdateScreen chooses either direct controller refreshes or
    // UpdateGameHackInput. Cover this branch too so settings-backed patches
    // never depend on which input-refresh policy is active.
    if (Control == 0 && m_JfgRuntime != nullptr)
    {
        m_JfgRuntime->ProcessRuntimeFrame();
        RefreshJfgInput();
    }

    const JFG_PORT_INPUT PortInput = JfgPortInput(Control);
    const bool JfgExclusiveInput =
        m_JfgRuntime != nullptr && m_JfgRuntime->UsesExclusiveInput(PortInput);
    if (JfgExclusiveInput)
    {
        Keys->Value = 0;
    }
    else
    {
        if (GetKeys == nullptr)
        {
            return;
        }
        GetKeys(Control, Keys);
    }

    if (m_JfgRuntime == nullptr)
    {
        return;
    }
    // Port one goes through even without a source so the runtime can stand
    // its patches down once every source routed to it is gone.
    if (JfgExclusiveInput || Control == 0)
    {
        m_JfgRuntime->ProcessController(Control, PortInput, *Keys);
    }
    SetGameInputCapture(m_JfgRuntime->UsesKeyboardMouse());
}

// Runs on the video interrupt so the mouse camera keeps up with the render rate
// while the buttons above stay on the game's own controller reads.
void CControl_Plugin::UpdateGameHackInput(void)
{
    if (m_JfgRuntime == nullptr)
    {
        return;
    }

    // Settings-backed runtime patches must keep ticking even when the selected
    // input plugin does not expose the optional keyboard/mouse API.
    m_JfgRuntime->ProcessRuntimeFrame();

    if (m_GetKeyboardMouseState == nullptr && m_GetGamepadState == nullptr)
    {
        return;
    }

    RefreshJfgInput();
    const JFG_PORT_INPUT PortInput = JfgPortInput(0);
    const bool JfgExclusiveInput = m_JfgRuntime->UsesExclusiveInput(PortInput);
    if (!JfgExclusiveInput && GetKeys == nullptr)
    {
        return;
    }

    BUTTONS Buttons = {};
    if (!JfgExclusiveInput)
    {
        GetKeys(0, &Buttons);
    }
    m_JfgRuntime->ProcessVideoFrame(PortInput, Buttons);
    SetGameInputCapture(m_JfgRuntime->UsesKeyboardMouse());
}

// One plugin poll per controller sweep: reading the mouse consumes its delta,
// so every port has to be served from the same snapshot. A gamepad only counts
// while it is switched on in the settings and actually attached, otherwise a
// port it is routed to would go dead instead of falling back to the plugin.
void CControl_Plugin::RefreshJfgInput(void)
{
    m_JfgInput.KeyboardMouseValid = false;
    if (m_GetKeyboardMouseState != nullptr && g_Settings->LoadBool(Setting_JfgKeyboardMouse))
    {
        KEYBOARD_MOUSE_STATE & State = m_JfgInput.KeyboardMouse;
        memset(&State, 0, sizeof(State));
        State.Size = sizeof(State);
        m_JfgInput.KeyboardMouseValid = m_GetKeyboardMouseState(&State) != 0;
    }

    const SettingID GamepadEnabled[] = {Setting_JfgGamepad1, Setting_JfgGamepad2};
    for (int32_t i = 0; i < 2; i++)
    {
        GAMEPAD_STATE & State = m_JfgInput.Gamepads[i];
        memset(&State, 0, sizeof(State));
        State.Size = sizeof(State);
        if (m_GetGamepadState == nullptr || !g_Settings->LoadBool(GamepadEnabled[i]) ||
            !m_GetGamepadState(i, &State))
        {
            State.Connected = 0;
        }
    }
    ApplyJfgPortPresence();
}

// Routes the snapshot's active sources to one N64 port following the port
// settings. Sources sharing a port are merged by the runtime.
JFG_PORT_INPUT CControl_Plugin::JfgPortInput(int32_t Control) const
{
    JFG_PORT_INPUT Input = {};
    if (m_JfgInput.KeyboardMouseValid &&
        (int32_t)g_Settings->LoadDword(Setting_JfgKeyboardMousePort) == Control)
    {
        Input.KeyboardMouse = &m_JfgInput.KeyboardMouse;
    }
    const SettingID GamepadPort[] = {Setting_JfgGamepad1Port, Setting_JfgGamepad2Port};
    for (int32_t i = 0; i < 2; i++)
    {
        if (m_JfgInput.Gamepads[i].Connected != 0 &&
            (int32_t)g_Settings->LoadDword(GamepadPort[i]) == Control)
        {
            Input.Gamepads[i] = &m_JfgInput.Gamepads[i];
        }
    }
    return Input;
}

// A JFG source can be routed to a port the input plugin left unplugged. The
// PIF answers the game's controller status from the plugin's CONTROL array, so
// plug a standard controller in there for as long as a source feeds the port
// and hand the plugin's own value back once none does.
void CControl_Plugin::ApplyJfgPortPresence(void)
{
    const bool Supported = m_JfgRuntime != nullptr && m_JfgRuntime->SupportsCurrentRom();
    for (int32_t Control = 0; Control < 4; Control++)
    {
        const bool Fed = Supported && JfgPortInput(Control).HasSource();
        CONTROL & Port = m_PluginControllers[Control];
        if (Fed && Port.Present != PRESENT_CONT)
        {
            Port.Present = PRESENT_CONT;
            Port.RawData = false;
            Port.Plugin = PLUGIN_NONE;
            m_JfgForcedPresent[Control] = true;
        }
        else if (!Fed && m_JfgForcedPresent[Control])
        {
            Port.Present = m_PluginPresent[Control];
            m_JfgForcedPresent[Control] = false;
        }
    }
}

void CControl_Plugin::GameStateSaving(void)
{
    if (m_JfgRuntime != nullptr)
    {
        m_JfgRuntime->StateSaving();
    }
}

void CControl_Plugin::GameStateLoaded(void)
{
    if (m_JfgRuntime != nullptr)
    {
        m_JfgRuntime->StateLoaded();
    }
}

void CControl_Plugin::ResetGameHack(void)
{
    if (m_JfgRuntime != nullptr)
    {
        m_JfgRuntime->Reset();
    }
    SetGameInputCapture(false);
}

void CControl_Plugin::SetGameInputCapture(bool Capture)
{
    Capture = Capture && m_SetKeyboardMouseCapture != nullptr;
    m_GameInputCaptured = Capture;
    if (m_SetKeyboardMouseCapture != nullptr)
    {
        m_SetKeyboardMouseCapture(Capture ? true : false);
    }
}

void CControl_Plugin::SetControl(CControl_Plugin const * const Plugin)
{
    if (m_AllocatedControllers)
    {
        for (int32_t count = 0; count < sizeof(m_Controllers) / sizeof(m_Controllers[0]); count++)
        {
            delete m_Controllers[count];
            m_Controllers[count] = nullptr;
        }
    }
    m_AllocatedControllers = false;
    for (int32_t count = 0; count < sizeof(m_Controllers) / sizeof(m_Controllers[0]); count++)
    {
        m_Controllers[count] = Plugin->m_Controllers[count];
    }
}

CCONTROL::CCONTROL(int32_t & Present, int32_t & RawData, int32_t & PlugType) :
    m_Present(Present), m_RawData(RawData), m_PlugType(PlugType)
{
    m_Buttons.Value = 0;
}
