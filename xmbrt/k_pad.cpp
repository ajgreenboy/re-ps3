// Controller input for sys_hid_manager_read: CellPadData's button array (24 big-endian u16, sensors on),
// filled from a DualSense (raw HID, USB input report 0x01) or, failing that, an XInput pad.
// Layout and neutral values follow RPCS3's cellPad pad_get_data.
#include "kernel.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstring>
#include <thread>

namespace
{
    // CellPadData button offsets
    enum : int { D1 = 2, D2 = 3, RX = 4, RY = 5, LX = 6, LY = 7, P_RIGHT = 8, P_LEFT, P_UP, P_DOWN, P_TRIANGLE, P_CIRCLE, P_CROSS,
        P_SQUARE, P_L1, P_R1, P_L2, P_R2, S_X = 20, S_Y, S_Z, S_G, LEN = 24 };
    // DIGITAL1 / DIGITAL2 bits
    constexpr uint16_t SELECT = 0x01, L3 = 0x02, R3 = 0x04, START = 0x08, UP = 0x10, RIGHT = 0x20, DOWN = 0x40, LEFT = 0x80, PS = 0x100;
    constexpr uint16_t L2 = 0x01, R2 = 0x02, L1 = 0x04, R1 = 0x08, TRIANGLE = 0x10, CIRCLE = 0x20, CROSS = 0x40, SQUARE = 0x80;

    std::mutex g_pad_mutex;
    uint16_t g_state[LEN];
    std::atomic<bool> g_connected{ false };
    std::atomic<uint32_t> g_keyboard_down{0}, g_keyboard_pressed{0};

    void neutral(uint16_t* s)
    {
        memset(s, 0, LEN * 2);
        s[0] = 0;
        s[1] = 0x70 | (LEN / 2);
        s[RX] = s[RY] = s[LX] = s[LY] = 0x80;
        s[S_X] = 512; s[S_Y] = 399; s[S_Z] = 512; s[S_G] = 512;
    }

    HANDLE open_dualsense()
    {
        GUID hid;
        HidD_GetHidGuid(&hid);
        HDEVINFO devs = SetupDiGetClassDevsW(&hid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
        if (devs == INVALID_HANDLE_VALUE) return INVALID_HANDLE_VALUE;
        HANDLE found = INVALID_HANDLE_VALUE;
        SP_DEVICE_INTERFACE_DATA iface{ sizeof(iface) };
        for (DWORD i = 0; found == INVALID_HANDLE_VALUE && SetupDiEnumDeviceInterfaces(devs, nullptr, &hid, i, &iface); i++)
        {
            DWORD need = 0;
            SetupDiGetDeviceInterfaceDetailW(devs, &iface, nullptr, 0, &need, nullptr);
            auto* detail = (SP_DEVICE_INTERFACE_DETAIL_DATA_W*)malloc(need);
            detail->cbSize = sizeof(*detail);
            if (SetupDiGetDeviceInterfaceDetailW(devs, &iface, detail, need, nullptr, nullptr))
            {
                HANDLE h = CreateFileW(detail->DevicePath, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
                if (h != INVALID_HANDLE_VALUE)
                {
                    HIDD_ATTRIBUTES attr{ sizeof(attr) };
                    PHIDP_PREPARSED_DATA pp = nullptr;
                    HIDP_CAPS caps{};
                    bool match = HidD_GetAttributes(h, &attr) && attr.VendorID == 0x054C && (attr.ProductID == 0x0CE6 || attr.ProductID == 0x0DF2);
                    // The gamepad collection (usage page 1, usage 5), not the audio/other interfaces
                    if (match && HidD_GetPreparsedData(h, &pp))
                    {
                        match = HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS && caps.UsagePage == 1 && caps.Usage == 5;
                        HidD_FreePreparsedData(pp);
                    }
                    if (match) found = h;
                    else CloseHandle(h);
                }
            }
            free(detail);
        }
        SetupDiDestroyDeviceInfoList(devs);
        return found;
    }

    void set_button(uint16_t* s, int digital, uint16_t bit, int pressure, bool pressed)
    {
        if (pressed) s[digital] |= bit;
        if (pressure >= 0) s[pressure] = pressed ? 255 : 0;
    }

    // DualSense USB report 0x01: [1..4] sticks, [5..6] triggers, [8] hat + face buttons, [9] shoulders/sticks/menu, [10] PS
    void from_dualsense(const uint8_t* r, uint16_t* s)
    {
        neutral(s);
        s[LX] = r[1]; s[LY] = r[2]; s[RX] = r[3]; s[RY] = r[4];
        const uint8_t hat = r[8] & 0x0F;
        const bool up = hat == 0 || hat == 1 || hat == 7, right = hat >= 1 && hat <= 3, down = hat >= 3 && hat <= 5, left = hat >= 5 && hat <= 7;
        set_button(s, D1, UP, P_UP, up);
        set_button(s, D1, RIGHT, P_RIGHT, right);
        set_button(s, D1, DOWN, P_DOWN, down);
        set_button(s, D1, LEFT, P_LEFT, left);
        set_button(s, D2, SQUARE, P_SQUARE, r[8] & 0x10);
        set_button(s, D2, CROSS, P_CROSS, r[8] & 0x20);
        set_button(s, D2, CIRCLE, P_CIRCLE, r[8] & 0x40);
        set_button(s, D2, TRIANGLE, P_TRIANGLE, r[8] & 0x80);
        set_button(s, D2, L1, P_L1, r[9] & 0x01);
        set_button(s, D2, R1, P_R1, r[9] & 0x02);
        if (r[9] & 0x04) s[D2] |= L2;
        if (r[9] & 0x08) s[D2] |= R2;
        s[P_L2] = r[5];
        s[P_R2] = r[6];
        set_button(s, D1, SELECT, -1, r[9] & 0x10); // Create
        set_button(s, D1, START, -1, r[9] & 0x20);  // Options
        set_button(s, D1, L3, -1, r[9] & 0x40);
        set_button(s, D1, R3, -1, r[9] & 0x80);
        set_button(s, D1, PS, -1, r[10] & 0x01);
    }

    void from_xinput(const XINPUT_GAMEPAD& g, uint16_t* s)
    {
        neutral(s);
        auto axis = [](SHORT v, bool invert) { int x = (v >> 8) + 128; if (invert) x = 255 - x; return uint16_t(std::clamp(x, 0, 255)); };
        s[LX] = axis(g.sThumbLX, false); s[LY] = axis(g.sThumbLY, true);
        s[RX] = axis(g.sThumbRX, false); s[RY] = axis(g.sThumbRY, true);
        const WORD b = g.wButtons;
        set_button(s, D1, UP, P_UP, b & XINPUT_GAMEPAD_DPAD_UP);
        set_button(s, D1, RIGHT, P_RIGHT, b & XINPUT_GAMEPAD_DPAD_RIGHT);
        set_button(s, D1, DOWN, P_DOWN, b & XINPUT_GAMEPAD_DPAD_DOWN);
        set_button(s, D1, LEFT, P_LEFT, b & XINPUT_GAMEPAD_DPAD_LEFT);
        set_button(s, D2, CROSS, P_CROSS, b & XINPUT_GAMEPAD_A);
        set_button(s, D2, CIRCLE, P_CIRCLE, b & XINPUT_GAMEPAD_B);
        set_button(s, D2, SQUARE, P_SQUARE, b & XINPUT_GAMEPAD_X);
        set_button(s, D2, TRIANGLE, P_TRIANGLE, b & XINPUT_GAMEPAD_Y);
        set_button(s, D2, L1, P_L1, b & XINPUT_GAMEPAD_LEFT_SHOULDER);
        set_button(s, D2, R1, P_R1, b & XINPUT_GAMEPAD_RIGHT_SHOULDER);
        if (g.bLeftTrigger > 30) s[D2] |= L2;
        if (g.bRightTrigger > 30) s[D2] |= R2;
        s[P_L2] = g.bLeftTrigger;
        s[P_R2] = g.bRightTrigger;
        set_button(s, D1, SELECT, -1, b & XINPUT_GAMEPAD_BACK);
        set_button(s, D1, START, -1, b & XINPUT_GAMEPAD_START);
        set_button(s, D1, L3, -1, b & XINPUT_GAMEPAD_LEFT_THUMB);
        set_button(s, D1, R3, -1, b & XINPUT_GAMEPAD_RIGHT_THUMB);
    }

    void pad_thread()
    {
        HANDLE ds = INVALID_HANDLE_VALUE;
        uint8_t report[128];
        for (;;)
        {
            uint16_t s[LEN];
            bool ok = false;
            if (ds == INVALID_HANDLE_VALUE)
            {
                ds = open_dualsense();
                if (ds != INVALID_HANDLE_VALUE) log_line("pad: DualSense connected");
            }
            if (ds != INVALID_HANDLE_VALUE)
            {
                DWORD got = 0;
                if (ReadFile(ds, report, sizeof(report), &got, nullptr) && got >= 11 && report[0] == 0x01)
                {
                    from_dualsense(report, s);
                    ok = true;
                }
                else if (!got)
                {
                    log_line("pad: DualSense lost");
                    CloseHandle(ds);
                    ds = INVALID_HANDLE_VALUE;
                }
            }
            if (!ok)
            {
                XINPUT_STATE xs{};
                if (XInputGetState(0, &xs) == ERROR_SUCCESS)
                {
                    from_xinput(xs.Gamepad, s);
                    ok = true;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(ds == INVALID_HANDLE_VALUE ? 8 : 0));
            }
            if (!ok)
            {
                neutral(s);
                if (ds == INVALID_HANDLE_VALUE) std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            std::lock_guard lock(g_pad_mutex);
            memcpy(g_state, s, sizeof(s));
            g_connected = ok;
        }
    }
}

void pad_init()
{
    neutral(g_state);
    std::thread(pad_thread).detach();
}

// Window-local keyboard controller: Home=PS, Enter=Cross, Escape=Circle,
// arrows=D-pad, S=Square, T=Triangle, Space=Start, Tab=Select.
bool pad_key(uint32_t key, bool down)
{
    if (!key) { g_keyboard_down=0;g_keyboard_pressed=0;return true; }
    uint32_t bits=0;
    switch (key)
    {
    case VK_HOME: bits=PS;break;
    case VK_RETURN: bits=uint32_t(CROSS)<<16;break;
    case VK_ESCAPE: bits=uint32_t(CIRCLE)<<16;break;
    case VK_UP: bits=UP;break;
    case VK_RIGHT: bits=RIGHT;break;
    case VK_DOWN: bits=DOWN;break;
    case VK_LEFT: bits=LEFT;break;
    case 'S': bits=uint32_t(SQUARE)<<16;break;
    case 'T': bits=uint32_t(TRIANGLE)<<16;break;
    case VK_SPACE: bits=START;break;
    case VK_TAB: bits=SELECT;break;
    default:return false;
    }
    if (down)
    {
        g_keyboard_down.fetch_or(bits);
        // Preserve a quick tap until at least one guest report observes it.
        g_keyboard_pressed.fetch_or(bits);
    }
    else g_keyboard_down.fetch_and(~bits);
    return true;
}

// Copies CellPadData.button (big-endian) for port 0; returns the number of u16 written.
uint32_t pad_read(uint32_t guest_buf, uint64_t max_u16)
{
    std::lock_guard lock(g_pad_mutex);
    uint16_t state[LEN];
    memcpy(state,g_state,sizeof(state));
    const uint32_t keys=g_keyboard_down.load()|g_keyboard_pressed.exchange(0);
    state[D1]|=uint16_t(keys);state[D2]|=uint16_t(keys>>16);
    for (const auto& button : {std::pair{UP,P_UP}, {RIGHT,P_RIGHT}, {DOWN,P_DOWN}, {LEFT,P_LEFT}})
        if(keys&button.first)state[button.second]=255;
    for (const auto& button : {std::pair{CROSS,P_CROSS}, {CIRCLE,P_CIRCLE}, {SQUARE,P_SQUARE}, {TRIANGLE,P_TRIANGLE}})
        if((keys>>16)&button.first)state[button.second]=255;
    const uint32_t n = uint32_t(std::min<uint64_t>(LEN, max_u16));
    for (uint32_t i = 0; i < n; i++) guest_store_u16(guest_buf + i * 2, state[i]);
    return n;
}
