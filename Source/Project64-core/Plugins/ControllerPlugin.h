#pragma once
#include <Project64-core/Plugins/PluginBase.h>
#include <Project64-plugin-spec/Input.h>

class CControl_Plugin;
class CJetForceGeminiRuntime;
struct JFG_PORT_INPUT;

class CCONTROL
{
public:
    CCONTROL(int32_t & Present, int32_t & RawData, int32_t & PlugType);
    inline bool Present(void) const
    {
        return m_Present != 0;
    }
    inline uint32_t Buttons(void) const
    {
        return m_Buttons.Value;
    }
    inline PluginType Plugin(void) const
    {
        return static_cast<PluginType>(m_PlugType);
    }

private:
    friend class CControl_Plugin;

    int32_t & m_Present;
    int32_t & m_RawData;
    int32_t & m_PlugType;
    BUTTONS m_Buttons;

    CCONTROL(void);
    CCONTROL(const CCONTROL &);
    CCONTROL & operator=(const CCONTROL &);
};

class CControl_Plugin : public CPlugin
{
public:
    typedef void(CALL * fnGetKeys)(int32_t Control, BUTTONS * Keys);
    typedef int32_t(CALL * fnGetKeyboardMouseState)(KEYBOARD_MOUSE_STATE * State);
    typedef void(CALL * fnSetKeyboardMouseCapture)(int32_t Capture);
    typedef int32_t(CALL * fnGetGamepadState)(int32_t Index, GAMEPAD_STATE * State);

    CControl_Plugin(void);
    ~CControl_Plugin();

    bool Initiate(CN64System * System, RenderWindow * Window);
    void SetControl(CControl_Plugin const * const Plugin);
    void GetControllerState(int32_t Control, BUTTONS * Keys);
    void ResetGameHack(void);
    void GameStateSaving(void);
    void GameStateLoaded(void);
    void UpdateGameHackInput(void);
    void UpdateKeys(void);

    void(CALL * WM_KeyDown)(uint32_t wParam, uint32_t lParam);
    void(CALL * WM_KeyUp)(uint32_t wParam, uint32_t lParam);
    void(CALL * WM_KillFocus)(uint32_t wParam, uint32_t lParam);
    void(CALL * RumbleCommand)(int32_t Control, int32_t bRumble);
    fnGetKeys GetKeys;
    void(CALL * ReadController)(int32_t Control, uint8_t * Command);
    void(CALL * ControllerCommand)(int32_t Control, uint8_t * Command);

    inline CCONTROL const * Controller(int32_t control)
    {
        return m_Controllers[control];
    }
    inline CONTROL * PluginControllers(void)
    {
        return m_PluginControllers;
    }

private:
    CControl_Plugin(const CControl_Plugin &);
    CControl_Plugin & operator=(const CControl_Plugin &);

    virtual int32_t GetDefaultSettingStartRange() const
    {
        return FirstCtrlDefaultSet;
    }
    virtual int32_t GetSettingStartRange() const
    {
        return FirstCtrlSettings;
    }
    PLUGIN_TYPE type()
    {
        return PLUGIN_TYPE_CONTROLLER;
    }
    bool LoadFunctions(void);
    void UnloadPluginDetails(void);
    void SetGameInputCapture(bool Capture);
    void RefreshJfgInput(void);
    JFG_PORT_INPUT JfgPortInput(int32_t Control) const;
    void ApplyJfgPortPresence(void);

    // The JFG input sources as last read from the plugin. Gamepad entries
    // report Connected == 0 when that pad is absent or the plugin lacks the
    // extension; see RefreshJfgInput.
    struct JFG_INPUT_SNAPSHOT
    {
        bool KeyboardMouseValid;
        KEYBOARD_MOUSE_STATE KeyboardMouse;
        GAMEPAD_STATE Gamepads[2];
    };

    bool m_AllocatedControllers;

    CONTROL m_PluginControllers[4];
    CCONTROL * m_Controllers[4];
    fnGetKeyboardMouseState m_GetKeyboardMouseState;
    fnSetKeyboardMouseCapture m_SetKeyboardMouseCapture;
    fnGetGamepadState m_GetGamepadState;
    CJetForceGeminiRuntime * m_JfgRuntime;
    bool m_GameInputCaptured;
    JFG_INPUT_SNAPSHOT m_JfgInput;
    // Presence the plugin itself reported for each port, restored when a port
    // stops being fed by a JFG source; see ApplyJfgPortPresence.
    int32_t m_PluginPresent[4];
    bool m_JfgForcedPresent[4];
};
