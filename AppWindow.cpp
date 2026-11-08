#include "AppWindow.h"
#include "AudioEngine.h"

#include <algorithm>
#include <cmath>
#include <cwchar>
#include <string>

namespace
{
constexpr UINT_PTR UI_TIMER_ID = 1;
constexpr UINT UI_TIMER_INTERVAL_MS = 50;
constexpr float MIN_SEMITONES = -12.0f;
constexpr float MAX_SEMITONES = 12.0f;

struct WindowState
{
    AudioEngine* engine = nullptr;
    bool waitingForKey = false;  // True after F2, until the next key press.
};

std::wstring KeyName(int virtualKey)
{
    UINT scanCode = MapVirtualKeyW(static_cast<UINT>(virtualKey), MAPVK_VK_TO_VSC);
    switch (virtualKey)
    {
    // These keys share scan codes with the number pad unless marked as extended.
    case VK_INSERT: case VK_DELETE: case VK_HOME: case VK_END:
    case VK_PRIOR: case VK_NEXT: case VK_LEFT: case VK_RIGHT:
    case VK_UP: case VK_DOWN: case VK_RCONTROL: case VK_RMENU:
        scanCode |= 0x100;
        break;
    }

    wchar_t name[64] = {};
    if (scanCode != 0 && GetKeyNameTextW(static_cast<LONG>(scanCode << 16), name, 64) > 0)
    {
        return name;
    }
    swprintf_s(name, L"Key 0x%02X", virtualKey);
    return name;
}

std::wstring BuildStatusText(const WindowState& ws)
{
    const AudioEngine& engine = *ws.engine;
    wchar_t line[512] = {};
    std::wstring text;

    swprintf_s(line, L"Output:  %ls%ls\n", engine.OutputName().c_str(),
        engine.UsingNamedOutput() ? L""
        : L"\n         (virtual cable not found - test mode, use headphones to avoid feedback)");
    text += line;
    swprintf_s(line, L"Input:   %ls\n\n", engine.InputName().c_str());
    text += line;

    if (ws.waitingForKey)
    {
        text += L"Push-to-talk: press the new key now (Esc to cancel)\n";
    }
    else
    {
        swprintf_s(line, L"Push-to-talk: hold %ls  (F2 to change)\n",
            KeyName(engine.PushToTalkKey()).c_str());
        text += line;
    }

    swprintf_s(line, L"Pitch:   %+.0f semitones  (Up/Down to adjust, 0 to reset)\n\n",
        engine.Semitones());
    text += line;

    if (!engine.IsRunning())
    {
        swprintf_s(line, L"Status:  STOPPED - audio error 0x%08lX (restart the app)\n",
            static_cast<unsigned long>(engine.ThreadError()));
    }
    else
    {
        swprintf_s(line, L"Status:  %ls\n", engine.IsTalking() ? L"TALKING" : L"muted");
    }
    text += line;

    swprintf_s(line, L"Underruns: %u   Dropped blocks: %u   Discontinuities: %u",
        engine.Underruns(), engine.DroppedBlocks(), engine.Discontinuities());
    text += line;
    return text;
}

void ChangePitch(AudioEngine& engine, float delta)
{
    const float value = delta == 0.0f ? 0.0f : engine.Semitones() + delta;
    engine.SetSemitones(std::clamp(std::round(value), MIN_SEMITONES, MAX_SEMITONES));
}

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (uMsg == WM_NCCREATE)
    {
        auto* createInfo = reinterpret_cast<CREATESTRUCTW*>(lParam);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA,
            reinterpret_cast<LONG_PTR>(createInfo->lpCreateParams));
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    auto* ws = reinterpret_cast<WindowState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (!ws)
    {
        return DefWindowProcW(hwnd, uMsg, wParam, lParam);
    }

    switch (uMsg)
    {
    case WM_KEYDOWN:
    case WM_SYSKEYDOWN:
        if (ws->waitingForKey)
        {
            if (wParam != VK_ESCAPE)
            {
                ws->engine->SetPushToTalkKey(static_cast<int>(wParam));
            }
            ws->waitingForKey = false;
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        if (uMsg == WM_KEYDOWN)
        {
            switch (wParam)
            {
            case VK_F2:  ws->waitingForKey = true;          break;
            case VK_UP:  ChangePitch(*ws->engine, 1.0f);    break;
            case VK_DOWN: ChangePitch(*ws->engine, -1.0f);  break;
            case '0': case VK_NUMPAD0: ChangePitch(*ws->engine, 0.0f); break;
            default: return 0;
            }
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_TIMER:
        if (wParam == UI_TIMER_ID)
        {
            InvalidateRect(hwnd, nullptr, FALSE);
            return 0;
        }
        break;

    case WM_ERASEBKGND:
        return 1;  // WM_PAINT fills the whole client area, which avoids flicker.

    case WM_PAINT:
    {
        PAINTSTRUCT ps = {};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT client = {};
        GetClientRect(hwnd, &client);

        // Draw off-screen, then copy, so the 50 ms refresh does not flicker.
        HDC memDc = CreateCompatibleDC(hdc);
        HBITMAP bitmap = CreateCompatibleBitmap(hdc, client.right, client.bottom);
        HGDIOBJ oldBitmap = SelectObject(memDc, bitmap);
        HGDIOBJ oldFont = SelectObject(memDc, GetStockObject(DEFAULT_GUI_FONT));

        FillRect(memDc, &client, reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1));
        SetBkMode(memDc, TRANSPARENT);
        RECT textRect = client;
        InflateRect(&textRect, -16, -16);
        const std::wstring text = BuildStatusText(*ws);
        DrawTextW(memDc, text.c_str(), -1, &textRect, DT_LEFT | DT_TOP | DT_EXPANDTABS);

        BitBlt(hdc, 0, 0, client.right, client.bottom, memDc, 0, 0, SRCCOPY);
        SelectObject(memDc, oldFont);
        SelectObject(memDc, oldBitmap);
        DeleteObject(bitmap);
        DeleteDC(memDc);
        EndPaint(hwnd, &ps);
        return 0;
    }

    case WM_DESTROY:
        KillTimer(hwnd, UI_TIMER_ID);
        PostQuitMessage(0);
        return 0;

    case WM_NCDESTROY:
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, 0);
        delete ws;
        return 0;
    }

    return DefWindowProcW(hwnd, uMsg, wParam, lParam);
}
}

HWND CreateAppWindow(HINSTANCE hInstance, AudioEngine& engine)
{
    const wchar_t CLASS_NAME[] = L"VoiceChangerWindow";

    WNDCLASSW wc = {};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassW(&wc))
    {
        return nullptr;
    }

    // Freed in WM_NCDESTROY.
    auto* state = new WindowState{ &engine };
    HWND hwnd = CreateWindowExW(0, CLASS_NAME, L"VoiceChanger",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 640, 280,
        nullptr, nullptr, hInstance, state);
    if (!hwnd)
    {
        // If WM_NCCREATE ran, WM_NCDESTROY already freed `state`; leak rather than double-free.
        return nullptr;
    }

    SetTimer(hwnd, UI_TIMER_ID, UI_TIMER_INTERVAL_MS, nullptr);
    return hwnd;
}
