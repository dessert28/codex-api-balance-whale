#include "pch.h"
#include "settings_window.hpp"
#include "../core/autostart.hpp"
#include "../overlay/desktop_overlay.hpp"

#include <gdiplus.h>
#include <windowsx.h>

#include <algorithm>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <sstream>
#include <string>

#pragma comment(lib, "gdiplus.lib")

namespace whale {
namespace {

using namespace Gdiplus;

constexpr wchar_t kSettingsClass[] = L"ApiBalanceWhaleSettingsWindow";
constexpr int kClientWidth = 520;
constexpr int kClientHeight = 640;
constexpr int kRowHeight = 52;
constexpr int kMargin = 24;
constexpr int kButtonWidth = 44;
constexpr int kValueWidth = 138;
constexpr int kControlLeft = kMargin + 150;
constexpr int kActionWidth = 150;
constexpr int kActionHeight = 42;

const Color kBackground(255, 247, 248, 251);
const Color kPanel(255, 255, 255, 255);
const Color kBorder(255, 210, 216, 230);
const Color kAccent(255, 32, 49, 112);
const Color kText(255, 32, 49, 112);
const Color kMuted(255, 120, 130, 150);

enum class Hotspot {
    None,
    SizeDown,
    SizeUp,
    Sound,
    SoundSet,
    HideDown,
    HideUp,
    TurnNotice,
    TurnDown,
    TurnUp,
    AutoClose,
    AutoStart,
    Save,
    ResetPosition,
    Close,
};

struct SettingsState {
    int size{440};
    int sound{1};
    int soundSet{0};
    int hideSeconds{5};
    int turnSeconds{6};
    int turnNotice{1};
    int autoClose{1};
    int x{INT_MIN};
    int y{INT_MIN};
};

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

int ReadInt(const std::string& text, const char* key, int fallback) {
    const std::regex pattern(std::string("\"") + key + "\"\\s*:\\s*(-?\\d+)");
    std::smatch match;
    if (std::regex_search(text, match, pattern)) {
        try {
            return std::stoi(match[1].str());
        } catch (...) {
            return fallback;
        }
    }
    return fallback;
}

SettingsState LoadState(const std::filesystem::path& configPath) {
    SettingsState state;
    const auto text = ReadAll(configPath);
    if (text.empty()) return state;
    state.size = ReadInt(text, "size", state.size);
    state.sound = ReadInt(text, "sound", state.sound);
    state.soundSet = ReadInt(text, "soundSet", state.soundSet);
    state.hideSeconds = ReadInt(text, "hideSeconds", state.hideSeconds);
    state.turnSeconds = ReadInt(text, "turnSeconds", state.turnSeconds);
    state.turnNotice = ReadInt(text, "turnNotice", state.turnNotice);
    state.autoClose = ReadInt(text, "autoClose", state.autoClose);
    state.x = ReadInt(text, "x", INT_MIN);
    state.y = ReadInt(text, "y", INT_MIN);
    return state;
}

bool SaveState(const std::filesystem::path& configPath, const SettingsState& state, bool keepPosition,
               std::wstring* error) {
    std::error_code code;
    std::filesystem::create_directories(configPath.parent_path(), code);
    std::ofstream output(configPath, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error) *error = L"无法写入 " + configPath.wstring();
        return false;
    }
    output << "{\"size\":" << state.size
           << ",\"sound\":" << (state.sound ? 1 : 0)
           << ",\"soundSet\":" << state.soundSet
           << ",\"hideSeconds\":" << state.hideSeconds
           << ",\"turnSeconds\":" << state.turnSeconds
           << ",\"turnNotice\":" << (state.turnNotice ? 1 : 0)
           << ",\"autoClose\":" << (state.autoClose ? 1 : 0);
    if (keepPosition && state.x != INT_MIN && state.y != INT_MIN) {
        output << ",\"x\":" << state.x << ",\"y\":" << state.y;
    }
    output << "}\n";
    return true;
}

void NotifyOverlay() {
    const HWND overlay = FindWindowW(kOverlayWindowClass, nullptr);
    if (!overlay) return;
    const UINT message = RegisterWindowMessageW(kOverlayReloadMessage);
    if (message != 0) PostMessageW(overlay, message, 0, 0);
}

class SettingsWindow {
public:
    explicit SettingsWindow(const SettingsPaths& paths) : m_paths(paths), m_state(LoadState(paths.configPath)) {}

    bool Create() {
        m_family = std::make_unique<FontFamily>(L"Microsoft YaHei UI");
        if (m_family->GetLastStatus() != Ok) m_family = std::make_unique<FontFamily>(L"Segoe UI");

        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &SettingsWindow::StaticProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kSettingsClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        RegisterClassExW(&windowClass);

        RECT frame{0, 0, kClientWidth, kClientHeight};
        AdjustWindowRectEx(&frame, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX, FALSE, WS_EX_TOOLWINDOW);
        const int windowWidth = frame.right - frame.left;
        const int windowHeight = frame.bottom - frame.top;
        const int screenWidth = GetSystemMetrics(SM_CXSCREEN);
        const int screenHeight = GetSystemMetrics(SM_CYSCREEN);
        const int x = std::max(40, (screenWidth - windowWidth) / 2 - 200);
        const int y = std::max(40, (screenHeight - windowHeight) / 2 - 120);

        m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kSettingsClass, L"Codex 配额小鲸鱼 设置",
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                                 x, y, windowWidth, windowHeight, nullptr, nullptr, windowClass.hInstance, this);
        if (!m_hwnd) return false;
        RECT client{};
        GetClientRect(m_hwnd, &client);
        m_clientHeight = client.bottom - client.top;
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        return true;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        return 0;
    }

private:
    static LRESULT CALLBACK StaticProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        SettingsWindow* self = reinterpret_cast<SettingsWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto created = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<SettingsWindow*>(created->lpCreateParams);
            self->m_hwnd = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(handle, message, wParam, lParam);
        return self->HandleMessage(message, wParam, lParam);
    }

    RECT RowRect(int index) const {
        const int top = kMargin + 56 + index * kRowHeight;
        return RECT{kControlLeft, top, kClientWidth - kMargin, top + kRowHeight - 10};
    }

    RECT MinusRect(int rowIndex) const {
        const auto row = RowRect(rowIndex);
        return RECT{row.left, row.top, row.left + kButtonWidth, row.bottom};
    }

    RECT ValueRect(int rowIndex) const {
        const auto row = RowRect(rowIndex);
        const int left = row.left + kButtonWidth;
        return RECT{left, row.top, left + kValueWidth, row.bottom};
    }

    RECT PlusRect(int rowIndex) const {
        const auto row = RowRect(rowIndex);
        const int left = row.left + kButtonWidth + kValueWidth;
        return RECT{left, row.top, left + kButtonWidth, row.bottom};
    }

    RECT ToggleRect(int rowIndex) const {
        const auto row = RowRect(rowIndex);
        return RECT{row.left, row.top, row.left + kValueWidth + kButtonWidth, row.bottom};
    }

    RECT ActionRect(int index) const {
        const int top = m_clientHeight - kMargin - kActionHeight;
        const int left = kMargin + index * (kActionWidth + 12);
        return RECT{left, top, left + kActionWidth, top + kActionHeight};
    }

    Hotspot HitTest(POINT point) const {
        const auto inside = [&](const RECT& rect) {
            return point.x >= rect.left && point.x < rect.right && point.y >= rect.top && point.y < rect.bottom;
        };
        if (inside(MinusRect(0))) return Hotspot::SizeDown;
        if (inside(PlusRect(0))) return Hotspot::SizeUp;
        if (inside(ToggleRect(1))) return Hotspot::Sound;
        if (inside(ToggleRect(2))) return Hotspot::SoundSet;
        if (inside(MinusRect(3))) return Hotspot::HideDown;
        if (inside(PlusRect(3))) return Hotspot::HideUp;
        if (inside(ToggleRect(4))) return Hotspot::TurnNotice;
        if (inside(MinusRect(5))) return Hotspot::TurnDown;
        if (inside(PlusRect(5))) return Hotspot::TurnUp;
        if (inside(ToggleRect(6))) return Hotspot::AutoClose;
        if (inside(ToggleRect(7))) return Hotspot::AutoStart;
        if (inside(ActionRect(0))) return Hotspot::Save;
        if (inside(ActionRect(1))) return Hotspot::ResetPosition;
        if (inside(ActionRect(2))) return Hotspot::Close;
        return Hotspot::None;
    }

    void DrawButton(Graphics& graphics, const RECT& rect, const std::wstring& text, bool accent) const {
        const RectF box(static_cast<REAL>(rect.left), static_cast<REAL>(rect.top),
                        static_cast<REAL>(rect.right - rect.left), static_cast<REAL>(rect.bottom - rect.top));
        SolidBrush fill(accent ? kAccent : kPanel);
        Pen border(kBorder, 1.0f);
        graphics.FillRectangle(&fill, box);
        graphics.DrawRectangle(&border, box);
        Font font(m_family.get(), 14.0f, FontStyleRegular, UnitPixel);
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        SolidBrush textBrush(accent ? Color(255, 255, 255, 255) : kText);
        graphics.DrawString(text.c_str(), static_cast<INT>(text.size()), &font, box, &format, &textBrush);
    }

    void DrawLabel(Graphics& graphics, int rowIndex, const wchar_t* text) const {
        const auto row = RowRect(rowIndex);
        Font font(m_family.get(), 15.0f, FontStyleRegular, UnitPixel);
        SolidBrush textBrush(kText);
        const RectF box(static_cast<REAL>(kMargin), static_cast<REAL>(row.top), 140.0f,
                        static_cast<REAL>(row.bottom - row.top));
        StringFormat format;
        format.SetLineAlignment(StringAlignmentCenter);
        graphics.DrawString(text, -1, &font, box, &format, &textBrush);
    }

    void Render() {
        PAINTSTRUCT paint{};
        HDC target = BeginPaint(m_hwnd, &paint);
        RECT client{};
        GetClientRect(m_hwnd, &client);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;

        Bitmap buffer(width, height, PixelFormat32bppPARGB);
        Graphics graphics(&buffer);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics.Clear(kBackground);

        Font titleFont(m_family.get(), 21.0f, FontStyleBold, UnitPixel);
        SolidBrush textBrush(kText);
        graphics.DrawString(L"Codex 配额小鲸鱼", -1, &titleFont, PointF(static_cast<REAL>(kMargin), 20.0f), nullptr, &textBrush);
        Font noteFont(m_family.get(), 12.0f, FontStyleRegular, UnitPixel);
        SolidBrush mutedBrush(kMuted);
        graphics.DrawString(L"用量来自本机 Codex 会话日志；不读取 auth.json，不联网。", -1, &noteFont,
                            PointF(static_cast<REAL>(kMargin), 46.0f), nullptr, &mutedBrush);

        DrawLabel(graphics, 0, L"大小");
        DrawButton(graphics, MinusRect(0), L"-", false);
        DrawButton(graphics, ValueRect(0), std::to_wstring(m_state.size) + L" px", false);
        DrawButton(graphics, PlusRect(0), L"+", false);

        DrawLabel(graphics, 1, L"点击音效");
        DrawButton(graphics, ToggleRect(1), m_state.sound ? L"开" : L"关", m_state.sound != 0);

        DrawLabel(graphics, 2, L"音效组");
        DrawButton(graphics, ToggleRect(2), m_state.soundSet == 1 ? L"小黄鸭" : L"原版", false);

        DrawLabel(graphics, 3, L"气泡时长");
        DrawButton(graphics, MinusRect(3), L"-", false);
        DrawButton(graphics, ValueRect(3), std::to_wstring(m_state.hideSeconds) + L" 秒", false);
        DrawButton(graphics, PlusRect(3), L"+", false);

        DrawLabel(graphics, 4, L"每轮提示");
        DrawButton(graphics, ToggleRect(4), m_state.turnNotice ? L"开" : L"关", m_state.turnNotice != 0);

        DrawLabel(graphics, 5, L"提示时长");
        DrawButton(graphics, MinusRect(5), L"-", false);
        DrawButton(graphics, ValueRect(5), std::to_wstring(m_state.turnSeconds) + L" 秒", false);
        DrawButton(graphics, PlusRect(5), L"+", false);

        DrawLabel(graphics, 6, L"自动收起");
        DrawButton(graphics, ToggleRect(6), m_state.autoClose ? L"开" : L"关", m_state.autoClose != 0);

        DrawLabel(graphics, 7, L"开机自启");
        DrawButton(graphics, ToggleRect(7), m_autoStart ? L"开" : L"关", m_autoStart);

        const auto actions = ActionRect(0);
        graphics.DrawString(L"每轮提示显示本轮模型与 token；配额气泡固定 10 秒，普通气泡用气泡时长。", -1, &noteFont,
                            PointF(static_cast<REAL>(kMargin), static_cast<REAL>(actions.top) - 30.0f), nullptr, &mutedBrush);
        graphics.DrawString(L"快捷键默认 Ctrl+Alt+W（被占用时自动换 Ctrl+Shift+W，以托盘提示为准）；关闭自动收起后气泡常驻。", -1, &noteFont,
                            PointF(static_cast<REAL>(kMargin), static_cast<REAL>(actions.top) - 52.0f), nullptr, &mutedBrush);
        if (!m_status.empty()) {
            graphics.DrawString(m_status.c_str(), -1, &noteFont,
                                PointF(static_cast<REAL>(kMargin), static_cast<REAL>(actions.top) - 74.0f), nullptr, &mutedBrush);
        }

        DrawButton(graphics, ActionRect(0), L"保存", true);
        DrawButton(graphics, ActionRect(1), L"恢复默认位置", false);
        DrawButton(graphics, ActionRect(2), L"关闭", false);

        Graphics screen(target);
        screen.DrawImage(&buffer, 0, 0, width, height);
        EndPaint(m_hwnd, &paint);
    }

    void HandleAction(Hotspot hotspot) {
        switch (hotspot) {
        case Hotspot::SizeDown:
            m_state.size = std::max(200, m_state.size - 20);
            break;
        case Hotspot::SizeUp:
            m_state.size = std::min(900, m_state.size + 20);
            break;
        case Hotspot::Sound:
            m_state.sound = m_state.sound ? 0 : 1;
            break;
        case Hotspot::SoundSet:
            m_state.soundSet = m_state.soundSet == 1 ? 0 : 1;
            break;
        case Hotspot::HideDown:
            m_state.hideSeconds = std::max(3, m_state.hideSeconds - 1);
            break;
        case Hotspot::HideUp:
            m_state.hideSeconds = std::min(120, m_state.hideSeconds + 1);
            break;
        case Hotspot::TurnNotice:
            m_state.turnNotice = m_state.turnNotice ? 0 : 1;
            break;
        case Hotspot::TurnDown:
            m_state.turnSeconds = std::max(3, m_state.turnSeconds - 1);
            break;
        case Hotspot::TurnUp:
            m_state.turnSeconds = std::min(120, m_state.turnSeconds + 1);
            break;
        case Hotspot::AutoClose:
            m_state.autoClose = m_state.autoClose ? 0 : 1;
            break;
        case Hotspot::AutoStart:
            m_autoStart = !m_autoStart;
            break;
        case Hotspot::Save: {
            std::wstring error;
            if (!SaveState(m_paths.configPath, m_state, true, &error)) {
                m_status = error;
                break;
            }
            if (m_autoStart != whale::IsAutoStartEnabled() && !whale::SetAutoStart(m_autoStart, &error)) {
                m_status = error;
                break;
            }
            NotifyOverlay();
            m_status = L"已保存，悬浮层已刷新。";
            break;
        }
        case Hotspot::ResetPosition: {
            std::wstring error;
            if (!SaveState(m_paths.configPath, m_state, false, &error)) {
                m_status = error;
                break;
            }
            NotifyOverlay();
            m_status = L"已恢复默认位置（右下角）。";
            break;
        }
        case Hotspot::Close:
            DestroyWindow(m_hwnd);
            return;
        default:
            break;
        }
        InvalidateRect(m_hwnd, nullptr, FALSE);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_PAINT:
            Render();
            return 0;
        case WM_SIZE:
            if (wParam != SIZE_MINIMIZED) {
                m_clientHeight = HIWORD(lParam);
                InvalidateRect(m_hwnd, nullptr, FALSE);
            }
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEMOVE: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const auto hotspot = HitTest(point);
            SetCursor(LoadCursorW(nullptr, hotspot == Hotspot::None ? IDC_ARROW : IDC_HAND));
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            const auto hotspot = HitTest(point);
            if (hotspot != Hotspot::None) HandleAction(hotspot);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }
    }

    SettingsPaths m_paths;
    SettingsState m_state;
    HWND m_hwnd{};
    std::unique_ptr<FontFamily> m_family;
    int m_clientHeight{kClientHeight};
    bool m_autoStart{IsAutoStartEnabled()};
    std::wstring m_status;
};

} // namespace

int RunSettingsWindow(const SettingsPaths& paths) {
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) return 1;
    int exitCode = 1;
    {
        SettingsWindow window(paths);
        if (window.Create()) exitCode = window.Run();
    }
    GdiplusShutdown(token);
    return exitCode;
}

} // namespace whale
