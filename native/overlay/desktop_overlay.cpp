#include "pch.h"
#include "desktop_overlay.hpp"
#include "../core/audio.hpp"
#include "../core/autostart.hpp"
#include "../core/bubble_policy.hpp"
#include "../core/quotes.hpp"
#include "../core/session_watcher.hpp"
#include "../core/supervisor.hpp"

#include <gdiplus.h>
#include <shellapi.h>
#include <windowsx.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <memory>
#include <random>
#include <regex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#pragma comment(lib, "gdiplus.lib")

namespace whale {

const wchar_t* const kOverlayWindowClass = L"ApiBalanceWhaleOverlayWindow";
const wchar_t* const kOverlayReloadMessage = L"ApiBalanceWhaleReloadOverlay";

namespace {

using namespace Gdiplus;

constexpr UINT kIdRefresh = 1;
constexpr UINT kIdHideBubble = 2;
constexpr UINT kIdSessionChange = 3;
constexpr UINT kMessageSnapshot = WM_APP + 1;
constexpr UINT kTrayCallbackMessage = WM_APP + 2;
constexpr UINT kMessageShowQuota = WM_APP + 3;
constexpr UINT kMessageSessionsChanged = WM_APP + 4;
constexpr UINT kRefreshIntervalMs = 5000;
// A running turn writes a burst of log lines, so the file watcher waits for a
// short quiet period instead of refreshing on every append.
constexpr UINT kSessionDebounceMs = 1500;
constexpr int kHotkeyShowQuota = 1;
constexpr int kSnapDistance = 24;
constexpr int kDragThreshold = 6;
constexpr BYTE kTransparentHitAlpha = 24;
constexpr UINT kTrayQuota = 1001;
constexpr UINT kTraySound = 1002;
constexpr UINT kTraySoundSet = 1003;
constexpr UINT kTraySize = 1004;
constexpr UINT kTrayAutoStart = 1005;
constexpr UINT kTraySettings = 1006;
constexpr UINT kTrayExit = 1007;
constexpr UINT kTraySessionExit = 1008;
constexpr UINT kTrayTurnNotice = 1009;
constexpr UINT kTrayAutoClose = 1010;
constexpr UINT kTrayHotkeyHint = 1011;
constexpr UINT kTrayQuotes = 1012;
constexpr int kSizePresets[] = {300, 440, 580};

struct HotkeyChoice {
    UINT modifiers;
    UINT virtualKey;
    const wchar_t* label;
};

// Ctrl+Alt+W is the upstream shortcut. Other applications are allowed to grab
// global hotkeys first, so the next free candidate is used instead and the
// tray shows which one is actually bound.
constexpr HotkeyChoice kHotkeyChoices[] = {
    {MOD_CONTROL | MOD_ALT, 'W', L"Ctrl+Alt+W"},
    {MOD_CONTROL | MOD_SHIFT, 'W', L"Ctrl+Shift+W"},
};

// Upstream widget geometry, in the 1026x1026 coordinate space used by the SVG
// asset so the bubble keeps the original proportions.
constexpr float kViewBox = 1026.0f;
constexpr float kStrokeWidth = 18.0f;
constexpr float kWhaleLeft = 416.0f;
constexpr float kWhaleTop = 416.0f;
constexpr float kWhaleSide = 610.0f;
constexpr float kBubbleCx = 454.0f;
constexpr float kBubbleCy = 247.0f;
constexpr float kBubbleRx = 373.0f;
constexpr float kBubbleRy = 232.0f;
constexpr float kTail1Cx = 352.0f;
constexpr float kTail1Cy = 561.0f;
constexpr float kTail1Rx = 37.5f;
constexpr float kTail1Ry = 26.0f;
constexpr float kTail2Cx = 442.0f;
constexpr float kTail2Cy = 646.0f;
constexpr float kTail2Rx = 24.5f;
constexpr float kTail2Ry = 18.0f;

const Color kBubbleFill(255, 255, 255, 255);
const Color kBubbleStroke(255, 32, 49, 112);
const Color kBubbleText(255, 83, 107, 169);
const Color kStaleText(255, 159, 176, 217);

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), wide.data(), length);
    return wide;
}

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string narrow(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), narrow.data(), length, nullptr, nullptr);
    return narrow;
}

std::wstring FormatCountdown(std::int64_t resetsAt) {
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(
                         std::chrono::system_clock::now().time_since_epoch())
                         .count();
    const auto seconds = std::max<std::int64_t>(0, resetsAt - now);
    const auto minutes = (seconds + 59) / 60;
    const auto days = minutes / 1440;
    const auto hours = (minutes % 1440) / 60;
    const auto rest = minutes % 60;
    std::wostringstream text;
    if (days) {
        text << days << L"天";
        if (hours) text << hours << L"小时";
        text << L"后重置";
    } else if (hours) {
        text << hours << L"小时";
        if (rest) text << rest << L"分钟";
        text << L"后重置";
    } else {
        text << rest << L"分钟后重置";
    }
    return text.str();
}

std::wstring QuotaLine(const wchar_t* label, const std::optional<UsageWindow>& window, bool stale) {
    std::wostringstream text;
    text << label << L"：";
    if (!window) {
        text << L"暂无数据";
    } else {
        text << L"已用 " << static_cast<int>(std::lround(window->usedPercent)) << L"% · "
             << FormatCountdown(window->resetsAt);
    }
    if (stale) text << L"（上次数据）";
    return text.str();
}

bool PointInEllipse(float x, float y, float cx, float cy, float rx, float ry) {
    if (rx <= 0 || ry <= 0) return false;
    const float dx = (x - cx) / rx;
    const float dy = (y - cy) / ry;
    return dx * dx + dy * dy <= 1.0f;
}

std::filesystem::path EnvironmentPath(const wchar_t* name, const std::filesystem::path& fallback) {
    if (const auto value = _wgetenv(name); value && *value) return value;
    return fallback;
}

void DebugLog(const std::wstring& message) {
    const auto target = _wgetenv(L"WHALE_OVERLAY_DEBUG");
    if (!target || !*target) return;
    // UTF-8, so Chinese text (quote lines, tray labels) stays readable in the log
    // instead of turning into question marks.
    std::ofstream output(std::filesystem::path(target), std::ios::app | std::ios::binary);
    if (!output) return;
    output << WideToUtf8(message) << '\n';
}

int ReadJsonInt(const std::string& text, const char* key, int fallback) {
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

class OverlayWindow {
public:
    explicit OverlayWindow(const OverlayOptions& options) : m_options(options) {}

    bool Create() {
        m_instanceMutex = CreateMutexW(nullptr, TRUE, L"Local\\ApiBalanceWhaleOverlayInstance");
        if (m_instanceMutex && GetLastError() == ERROR_ALREADY_EXISTS) {
            if (const HWND existing = FindWindowW(kOverlayWindowClass, nullptr)) {
                PostMessageW(existing, kMessageShowQuota, 0, 0);
            }
            return false;
        }
        LoadConfig();
        m_size = std::clamp(m_options.size, 200, 900);
        m_unit = static_cast<float>(m_size) / kViewBox;
        if (!CreateCanvas()) return false;
        LoadWhale();
        if (DumpRequested()) return false;
        if (!CreateWindowHandle()) return false;
        m_reloadMessage = RegisterWindowMessageW(kOverlayReloadMessage);
        m_audio = std::make_unique<AudioPlayer>(m_options.assetPath.parent_path());
        InitTray();
        RegisterQuotaHotkey();
        Render();
        Present();
        ShowWindow(m_hwnd, SW_SHOWNOACTIVATE);
        {
            RECT rect{};
            GetWindowRect(m_hwnd, &rect);
            std::wostringstream log;
            log << L"overlay created rect=" << rect.left << L"," << rect.top << L"," << rect.right << L"," << rect.bottom
                << L" visible=" << (IsWindowVisible(m_hwnd) ? 1 : 0)
                << L" exstyle=0x" << std::hex << GetWindowLongW(m_hwnd, GWL_EXSTYLE)
                << L" whale=" << (m_whale ? 1 : 0) << L" size=" << std::dec << m_size
                << L" unit=" << m_unit;
            DebugLog(log.str());
        }
        SetTimer(m_hwnd, kIdRefresh, kRefreshIntervalMs, nullptr);
        StartSessionWatcher();
        RefreshAsync();
        return true;
    }

    bool DumpRequested() {
        const auto target = _wgetenv(L"WHALE_OVERLAY_DUMP");
        if (!target || !*target) return false;
        BuildQuotaLines();
        m_bubble = Bubble::Quota;
        Render();
        CLSID encoder{};
        if (GetEncoderClsid(L"image/png", &encoder)) {
            m_canvas->Save(target, &encoder, nullptr);
        }
        return true;
    }

    static bool GetEncoderClsid(const wchar_t* mimeType, CLSID* clsid) {
        UINT count = 0;
        UINT bytes = 0;
        if (GetImageEncodersSize(&count, &bytes) != Ok || bytes == 0) return false;
        std::vector<BYTE> buffer(bytes);
        auto* encoders = reinterpret_cast<ImageCodecInfo*>(buffer.data());
        if (GetImageEncoders(count, bytes, encoders) != Ok) return false;
        for (UINT index = 0; index < count; ++index) {
            if (std::wcscmp(encoders[index].MimeType, mimeType) == 0) {
                *clsid = encoders[index].Clsid;
                return true;
            }
        }
        return false;
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
    enum class Bubble {
        Hidden,
        Quota,
        Turn,
        Quote,
    };

    static LRESULT CALLBACK StaticProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        OverlayWindow* self = reinterpret_cast<OverlayWindow*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto created = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<OverlayWindow*>(created->lpCreateParams);
            self->m_hwnd = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(handle, message, wParam, lParam);
        return self->HandleMessage(message, wParam, lParam);
    }

    bool CreateCanvas() {
        m_canvas = std::make_unique<Bitmap>(m_size, m_size, PixelFormat32bppPARGB);
        if (m_canvas->GetLastStatus() != Ok) return false;

        // UpdateLayeredWindow needs premultiplied pixels, and GDI+
        // GetHBITMAP drops the alpha channel on some versions, so the layers
        // are copied into a DIB section we own.
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = m_size;
        info.bmiHeader.biHeight = -m_size;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        info.bmiHeader.biCompression = BI_RGB;

        HDC screen = GetDC(nullptr);
        m_memoryDc = CreateCompatibleDC(screen);
        ReleaseDC(nullptr, screen);
        if (!m_memoryDc) return false;
        m_dib = CreateDIBSection(m_memoryDc, &info, DIB_RGB_COLORS, &m_dibBits, nullptr, 0);
        if (!m_dib || !m_dibBits) return false;
        SelectObject(m_memoryDc, m_dib);
        return true;
    }

    void LoadWhale() {
        auto candidate = m_options.assetPath;
        if (candidate.empty() || !std::filesystem::exists(candidate)) {
            wchar_t module[MAX_PATH]{};
            GetModuleFileNameW(nullptr, module, MAX_PATH);
            const auto moduleDir = std::filesystem::path(module).parent_path();
            candidate = moduleDir / L"assets" / L"DSniang1.png";
            if (!std::filesystem::exists(candidate)) {
                candidate = moduleDir.parent_path().parent_path() / L"assets" / L"DSniang1.png";
            }
        }
        if (!std::filesystem::exists(candidate)) return;
        auto image = std::make_unique<Image>(candidate.c_str());
        if (image->GetLastStatus() == Ok) m_whale = std::move(image);
        m_family = std::make_unique<FontFamily>(L"Microsoft YaHei UI");
        if (m_family->GetLastStatus() != Ok) m_family = std::make_unique<FontFamily>(L"Segoe UI");
    }

    bool CreateWindowHandle() {
        m_instance = GetModuleHandleW(nullptr);
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &OverlayWindow::StaticProc;
        windowClass.hInstance = m_instance;
        windowClass.lpszClassName = kOverlayWindowClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = nullptr;
        RegisterClassExW(&windowClass);

        const auto work = WorkArea();
        const int width = work.right - work.left;
        const int height = work.bottom - work.top;
        int x = m_options.hasPosition ? m_options.x : work.left + width - m_size - 12;
        int y = m_options.hasPosition ? m_options.y : work.top + height - m_size - 12;
        x = std::clamp(x, static_cast<int>(work.left) - m_size / 2, static_cast<int>(work.right) - m_size / 2);
        y = std::clamp(y, static_cast<int>(work.top) - m_size / 2, static_cast<int>(work.bottom) - m_size / 2);

        m_hwnd = CreateWindowExW(
            WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_TOPMOST | WS_EX_NOACTIVATE,
            kOverlayWindowClass, L"Codex Balance Whale", WS_POPUP, x, y, m_size, m_size,
            nullptr, nullptr, m_instance, this);
        return m_hwnd != nullptr;
    }

    RECT WorkArea() const {
        RECT area{};
        if (!SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0)) {
            area = RECT{0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN)};
        }
        return area;
    }

    void LoadConfig() {
        m_options.hasPosition = false;
        m_policy.hideSeconds = m_options.hideSeconds;
        std::ifstream input(m_options.configPath, std::ios::binary);
        if (!input) return;
        const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
        if (text.empty()) return;
        m_options.size = ReadJsonInt(text, "size", m_options.size);
        m_options.soundEnabled = ReadJsonInt(text, "sound", m_options.soundEnabled ? 1 : 0) != 0;
        m_options.soundSet = std::clamp(ReadJsonInt(text, "soundSet", m_options.soundSet), 0, 1);
        m_policy.hideSeconds = std::clamp(ReadJsonInt(text, "hideSeconds", m_policy.hideSeconds), 3, 120);
        m_policy.turnSeconds = std::clamp(ReadJsonInt(text, "turnSeconds", m_policy.turnSeconds), 3, 120);
        m_policy.autoClose = ReadJsonInt(text, "autoClose", m_policy.autoClose ? 1 : 0) != 0;
        m_turnNotice = ReadJsonInt(text, "turnNotice", m_turnNotice ? 1 : 0) != 0;
        if (const auto quotes = ParseQuoteJson(text); !quotes.empty()) m_quotes = quotes;
        m_soundEnabled = m_options.soundEnabled;
        m_soundSet = m_options.soundSet;
        const int x = ReadJsonInt(text, "x", INT_MIN);
        const int y = ReadJsonInt(text, "y", INT_MIN);
        if (x != INT_MIN && y != INT_MIN) {
            m_options.x = x;
            m_options.y = y;
            m_options.hasPosition = true;
        }
    }

    void SaveConfig() const {
        if (m_options.configPath.empty()) return;
        std::error_code error;
        std::filesystem::create_directories(m_options.configPath.parent_path(), error);
        RECT rect{};
        GetWindowRect(m_hwnd, &rect);
        std::ofstream output(m_options.configPath, std::ios::binary | std::ios::trunc);
        if (!output) return;
        output << "{\"size\":" << m_size
               << ",\"x\":" << rect.left
               << ",\"y\":" << rect.top
               << ",\"sound\":" << (m_soundEnabled ? 1 : 0)
               << ",\"soundSet\":" << m_soundSet
               << ",\"hideSeconds\":" << m_policy.hideSeconds
               << ",\"turnSeconds\":" << m_policy.turnSeconds
               << ",\"autoClose\":" << (m_policy.autoClose ? 1 : 0)
               << ",\"turnNotice\":" << (m_turnNotice ? 1 : 0)
               << ",\"quotes\":" << QuoteJson(m_quotes) << "}\n";
    }

    void Render() {
        if (!m_canvas) return;
        Graphics graphics(m_canvas.get());
        graphics.SetCompositingMode(CompositingModeSourceOver);
        graphics.SetCompositingQuality(CompositingQualityHighQuality);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(PixelOffsetModeHalf);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetTextRenderingHint(TextRenderingHintAntiAlias);
        graphics.Clear(Color(0, 0, 0, 0));

        if (m_whale) {
            const RectF box(kWhaleLeft * m_unit, kWhaleTop * m_unit, kWhaleSide * m_unit, kWhaleSide * m_unit);
            graphics.DrawImage(m_whale.get(), box);
        }
        if (m_bubble == Bubble::Hidden || m_lines.empty()) return;

        const Pen pen(kBubbleStroke, kStrokeWidth * m_unit);
        const SolidBrush fill(kBubbleFill);
        DrawEllipse(graphics, pen, fill, kTail1Cx, kTail1Cy, kTail1Rx, kTail1Ry);
        DrawEllipse(graphics, pen, fill, kTail2Cx, kTail2Cy, kTail2Rx, kTail2Ry);
        DrawEllipse(graphics, pen, fill, kBubbleCx, kBubbleCy, kBubbleRx, kBubbleRy);
        DrawLines(graphics);
    }

    void DrawEllipse(Graphics& graphics, const Pen& pen, const SolidBrush& fill,
                     float cx, float cy, float rx, float ry) const {
        const RectF box((cx - rx) * m_unit, (cy - ry) * m_unit, 2 * rx * m_unit, 2 * ry * m_unit);
        graphics.FillEllipse(&fill, box);
        graphics.DrawEllipse(&pen, box);
    }

    void DrawLines(Graphics& graphics) {
        if (!m_family || m_lines.empty()) return;
        float fontPixels = 0;
        float lineHeight = 0;
        // m_lineWidths were measured at 40px, so scale by candidate / 40 when
        // checking whether a line still fits inside the bubble ellipse.
        for (float units = 76.0f; units >= 24.0f; units -= 1.0f) {
            const float candidate = units * m_unit;
            const float perLine = candidate * 1.24f;
            const float total = perLine * static_cast<float>(m_lines.size());
            if (total > 2.0f * kBubbleRy * m_unit * 0.86f) continue;
            bool fits = true;
            for (std::size_t index = 0; index < m_lines.size(); ++index) {
                const float dy = -total / 2.0f + perLine * (static_cast<float>(index) + 0.5f);
                const float ratio = (dy / m_unit) / kBubbleRy;
                const float inside = 1.0f - ratio * ratio;
                const float halfWidth = kBubbleRx * std::sqrt(inside > 0.0f ? inside : 0.0f) * 0.92f;
                const float available = 2.0f * halfWidth * m_unit;
                if (m_lineWidths[index] * (candidate / 40.0f) > available) {
                    fits = false;
                    break;
                }
            }
            if (!fits) continue;
            fontPixels = candidate;
            lineHeight = perLine;
            break;
        }
        if (fontPixels <= 0) {
            fontPixels = 24.0f * m_unit;
            lineHeight = fontPixels * 1.24f;
        }
        Font font(m_family.get(), fontPixels, FontStyleBold, UnitPixel);
        const float total = lineHeight * static_cast<float>(m_lines.size());
        const float top = kBubbleCy * m_unit - total / 2.0f;
        StringFormat format;
        format.SetAlignment(StringAlignmentCenter);
        format.SetLineAlignment(StringAlignmentCenter);
        format.SetFormatFlags(StringFormatFlagsNoWrap);
        for (std::size_t index = 0; index < m_lines.size(); ++index) {
            const RectF box((kBubbleCx - kBubbleRx) * m_unit, top + lineHeight * static_cast<float>(index),
                            2.0f * kBubbleRx * m_unit, lineHeight);
            const bool stale = m_staleLines[index];
            SolidBrush brush(stale ? kStaleText : kBubbleText);
            graphics.DrawString(m_lines[index].c_str(), static_cast<INT>(m_lines[index].size()),
                                &font, box, &format, &brush);
        }
    }

    bool Present() {
        if (!m_hwnd || !m_canvas || !m_memoryDc || !m_dibBits) return false;
        if (!SyncPixels()) {
            DebugLog(L"SyncPixels failed");
            return false;
        }
        {
            const auto* pixels = static_cast<const BYTE*>(m_dibBits);
            std::size_t opaque = 0;
            std::size_t partialAlpha = 0;
            for (std::size_t index = 0; index < static_cast<std::size_t>(m_size) * m_size; index += 7) {
                const BYTE alpha = pixels[index * 4 + 3];
                if (alpha == 255) ++opaque;
                else if (alpha > 0) ++partialAlpha;
            }
            std::wostringstream log;
            log << L"dib sample opaque=" << opaque << L" partial=" << partialAlpha
                << L" firstPixel=" << std::hex << static_cast<int>(pixels[0]) << L","
                << static_cast<int>(pixels[1]) << L"," << static_cast<int>(pixels[2]) << L","
                << static_cast<int>(pixels[3]) << std::dec;
            DebugLog(log.str());
        }
        HDC screen = GetDC(nullptr);
        RECT rect{};
        GetWindowRect(m_hwnd, &rect);
        POINT destination{rect.left, rect.top};
        SIZE size{m_size, m_size};
        POINT source{0, 0};
        BLENDFUNCTION blend{AC_SRC_OVER, 0, 255, AC_SRC_ALPHA};
        const BOOL ok = UpdateLayeredWindow(m_hwnd, screen, &destination, &size, m_memoryDc, &source, 0, &blend, ULW_ALPHA);
        ReleaseDC(nullptr, screen);
        if (!ok) {
            std::wostringstream log;
            log << L"UpdateLayeredWindow failed: " << GetLastError();
            DebugLog(log.str());
        }
        return ok != FALSE;
    }

    bool SyncPixels() {
        if (!m_canvas || !m_dibBits) return false;
        Rect rect(0, 0, m_size, m_size);
        BitmapData data{};
        if (m_canvas->LockBits(&rect, ImageLockModeRead, PixelFormat32bppPARGB, &data) != Ok) return false;
        const int stride = m_size * 4;
        const auto* source = static_cast<const BYTE*>(data.Scan0);
        auto* target = static_cast<BYTE*>(m_dibBits);
        for (int row = 0; row < m_size; ++row) {
            const int sourceRow = data.Stride >= 0 ? row : (m_size - 1 - row);
            std::memcpy(target + static_cast<std::size_t>(row) * stride,
                        source + static_cast<std::size_t>(sourceRow) * std::abs(data.Stride),
                        static_cast<std::size_t>(stride));
        }
        m_canvas->UnlockBits(&data);
        return true;
    }

    BYTE AlphaAt(POINT local) const {
        if (!m_dibBits) return 0;
        if (local.x < 0 || local.y < 0 || local.x >= m_size || local.y >= m_size) return 0;
        const auto* pixel = static_cast<const BYTE*>(m_dibBits) +
                            (static_cast<std::size_t>(local.y) * m_size + local.x) * 4;
        return pixel[3];
    }

    void SetLines(std::vector<std::wstring> lines, std::vector<bool> stale) {
        m_lines = std::move(lines);
        m_staleLines = std::move(stale);
        MeasureLineWidths();
    }

    void MeasureLineWidths() {
        m_lineWidths.assign(m_lines.size(), 0.0f);
        if (!m_family || m_lines.empty()) return;
        Graphics graphics(m_canvas.get());
        const Font font(m_family.get(), 40.0f, FontStyleBold, UnitPixel);
        const StringFormat format(StringFormat::GenericTypographic());
        for (std::size_t index = 0; index < m_lines.size(); ++index) {
            RectF bounds;
            graphics.MeasureString(m_lines[index].c_str(), static_cast<INT>(m_lines[index].size()),
                                   &font, PointF(0, 0), &format, &bounds);
            m_lineWidths[index] = bounds.Width;
        }
    }

    // The watcher knows when the session logs move, so the overlay can refresh
    // right away instead of waiting for the next poll. forceProbe skips the
    // short lived cache, otherwise the fresh read would return cached numbers.
    void RefreshAsync(bool forceProbe = false) {
        if (m_scanInFlight.exchange(true)) {
            m_scanQueued.store(true);
            return;
        }
        const auto codexHome = m_options.codexHome;
        const auto statePath = m_options.statePath;
        const HWND handle = m_hwnd;
        std::thread([codexHome, statePath, handle, forceProbe] {
            auto* snapshot = new UsageSnapshot(ReadUsageSnapshot(codexHome, statePath, std::chrono::system_clock::now(), forceProbe));
            if (!PostMessageW(handle, kMessageSnapshot, 0, reinterpret_cast<LPARAM>(snapshot))) {
                delete snapshot;
            }
        }).detach();
    }

    void StartSessionWatcher() {
        if (m_options.codexHome.empty()) return;
        const HWND handle = m_hwnd;
        m_watcher = std::make_unique<SessionWatcher>(m_options.codexHome / "sessions", [handle] {
            PostMessageW(handle, kMessageSessionsChanged, 0, 0);
        });
        m_watcher->Start();
    }

    void ApplySnapshot(const UsageSnapshot& snapshot) {
        m_scanInFlight.store(false);
        // A change that arrived while this scan was running still has to be
        // seen, otherwise it waits for the next poll and the bubble is late.
        if (m_scanQueued.exchange(false)) RefreshAsync(true);
        m_snapshot = snapshot;
        m_hasSnapshot = true;
        if (const auto turn = m_monitor.Update(m_snapshot)) {
            if (m_turnNotice) {
                ShowTurnNotice(*turn);
                return;
            }
        }
        if (m_bubble != Bubble::Hidden) {
            if (m_bubble == Bubble::Quota) BuildQuotaLines();
            Render();
            Present();
        }
    }

    void BuildQuotaLines() {
        const bool stale = m_snapshot.stale || !m_snapshot.error.empty();
        SetLines({QuotaLine(L"5 小时", m_snapshot.fiveHour, stale),
                  QuotaLine(L"本周", m_snapshot.weekly, stale)},
                 {stale, stale});
    }

    void ShowQuota() {
        BuildQuotaLines();
        m_bubble = Bubble::Quota;
        ArmHideTimer(BubbleKind::Quota);
        Render();
        Present();
    }

    void ShowQuote() {
        if (m_quotes.empty()) m_quotes = DefaultQuotes();
        m_quoteIndex = PickQuote(m_quotes, m_quoteIndex, m_generator);
        const auto& line = m_quotes[m_quoteIndex];
        std::wostringstream log;
        log << L"quote pick=" << m_quoteIndex << L" of " << m_quotes.size() << L" weight=" << line.weight << L" text=" << line.text;
        DebugLog(log.str());
        SetLines({line.text}, {false});
        m_bubble = Bubble::Quote;
        ArmHideTimer(BubbleKind::Quote);
        Render();
        Present();
    }

    void ShowTurnNotice(const LastTurn& turn) {
        const bool stale = m_snapshot.stale || !m_snapshot.error.empty();
        std::wostringstream header;
        const auto model = Utf8ToWide(turn.model);
        header << (model.empty() ? L"本轮" : model) << L" · 本轮 " << turn.deltaTokens << L" tokens";
        SetLines({header.str(),
                  QuotaLine(L"5 小时", m_snapshot.fiveHour, stale),
                 QuotaLine(L"本周", m_snapshot.weekly, stale)},
                 {false, stale, stale});
        m_bubble = Bubble::Turn;
        ArmHideTimer(BubbleKind::Turn);
        Render();
        Present();
    }

    void HideBubble() {
        KillTimer(m_hwnd, kIdHideBubble);
        m_bubble = Bubble::Hidden;
        Render();
        Present();
    }

    BubbleKind CurrentKind() const {
        switch (m_bubble) {
        case Bubble::Quota: return BubbleKind::Quota;
        case Bubble::Turn: return BubbleKind::Turn;
        default: return BubbleKind::Quote;
        }
    }

    // Every scene carries its own lifetime: the quota card keeps the upstream
    // 10 s, the per-turn notice its own delay, and a random line the bubble
    // delay. A zero result means the bubble stays until the user clicks it.
    void ArmHideTimer(BubbleKind kind) {
        KillTimer(m_hwnd, kIdHideBubble);
        const int ttlMs = BubbleTtlMs(kind, m_policy);
        if (ttlMs > 0) SetTimer(m_hwnd, kIdHideBubble, static_cast<UINT>(ttlMs), nullptr);
    }

    HICON LoadTrayIcon() const {
        const auto png = m_options.assetPath;
        if (png.empty() || !std::filesystem::exists(png)) return nullptr;
        Bitmap source(png.c_str());
        if (source.GetLastStatus() != Ok) return nullptr;
        Bitmap scaled(32, 32, PixelFormat32bppPARGB);
        Graphics graphics(&scaled);
        graphics.SetInterpolationMode(InterpolationModeHighQualityBicubic);
        graphics.SetSmoothingMode(SmoothingModeAntiAlias);
        graphics.DrawImage(&source, Rect(0, 0, 32, 32));
        HICON icon = nullptr;
        scaled.GetHICON(&icon);
        return icon;
    }

    void InitTray() {
        m_tray = {};
        m_tray.cbSize = sizeof(m_tray);
        m_tray.hWnd = m_hwnd;
        m_tray.uID = 1;
        m_tray.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
        m_tray.uCallbackMessage = kTrayCallbackMessage;
        m_trayIcon = LoadTrayIcon();
        m_tray.hIcon = m_trayIcon ? m_trayIcon : LoadIconW(nullptr, IDI_APPLICATION);
        wcscpy_s(m_tray.szTip, L"Codex 配额小鲸鱼");
        m_trayAdded = Shell_NotifyIconW(NIM_ADD, &m_tray) != FALSE;
        if (m_trayAdded) {
            m_tray.uVersion = NOTIFYICON_VERSION_4;
            Shell_NotifyIconW(NIM_SETVERSION, &m_tray);
        }
        DebugLog(m_trayAdded ? L"tray icon added" : L"tray icon add failed");
    }

    void RemoveTray() {
        if (m_trayAdded) Shell_NotifyIconW(NIM_DELETE, &m_tray);
        m_trayAdded = false;
        if (m_trayIcon) {
            DestroyIcon(m_trayIcon);
            m_trayIcon = nullptr;
        }
    }

    int SizePercent() const {
        return static_cast<int>(std::lround(100.0 * m_size / kSizePresets[1]));
    }

    // Ctrl+Alt+W is the upstream global shortcut; the overlay owns the
    // window the hotkey is registered against.
    void RegisterQuotaHotkey() {
        for (const auto& choice : kHotkeyChoices) {
            if (!RegisterHotKey(m_hwnd, kHotkeyShowQuota, choice.modifiers, choice.virtualKey)) continue;
            m_hotkeyRegistered = true;
            m_hotkeyLabel = choice.label;
            break;
        }
        DebugLog(m_hotkeyRegistered ? L"hotkey " + m_hotkeyLabel + L" registered" : L"hotkey registration failed");
    }

    void ToggleQuota() {
        if (m_bubble == Bubble::Quota) HideBubble();
        else ShowQuota();
    }

    void ShowTrayMenu() {
        POINT cursor{};
        GetCursorPos(&cursor);
        HMENU menu = CreatePopupMenu();
        if (!menu) return;
        AppendMenuW(menu, MF_STRING, kTrayQuota, L"查看配额");
        const std::wstring hotkeyHint =
            L"快捷键：" + (m_hotkeyLabel.empty() ? std::wstring(L"不可用（已被占用）") : m_hotkeyLabel);
        AppendMenuW(menu, MF_STRING | MF_DISABLED, kTrayHotkeyHint, hotkeyHint.c_str());
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING | (m_soundEnabled ? MF_CHECKED : 0), kTraySound, L"音效");
        AppendMenuW(menu, MF_STRING | (m_soundSet == 1 ? MF_CHECKED : 0), kTraySoundSet, L"音效组：小黄鸭");
        std::wostringstream sizeLabel;
        sizeLabel << L"大小：" << SizePercent() << L"%";
        AppendMenuW(menu, MF_STRING, kTraySize, sizeLabel.str().c_str());
        AppendMenuW(menu, MF_STRING | (whale::IsAutoStartEnabled() ? MF_CHECKED : 0), kTrayAutoStart, L"开机自启");
        AppendMenuW(menu, MF_STRING | (m_turnNotice ? MF_CHECKED : 0), kTrayTurnNotice, L"每轮提示");
        AppendMenuW(menu, MF_STRING | (m_policy.autoClose ? MF_CHECKED : 0), kTrayAutoClose, L"气泡自动收起");
        AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
        AppendMenuW(menu, MF_STRING, kTraySettings, L"设置…");
        AppendMenuW(menu, MF_STRING, kTrayQuotes, L"随机语句…");
        AppendMenuW(menu, MF_STRING, kTraySessionExit, L"本次退出挂件（下次启动 Codex 恢复）");
        AppendMenuW(menu, MF_STRING, kTrayExit, L"完全退出");

        SetForegroundWindow(m_hwnd);
        const int command = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_RIGHTBUTTON, cursor.x, cursor.y, 0, m_hwnd, nullptr);
        DestroyMenu(menu);
        PostMessageW(m_hwnd, WM_NULL, 0, 0);
        if (command) HandleTrayCommand(static_cast<UINT>(command));
    }

    void HandleTrayCommand(UINT command) {
        switch (command) {
        case kTrayQuota:
            ShowQuota();
            break;
        case kTraySound:
            m_soundEnabled = !m_soundEnabled;
            if (m_soundEnabled && m_audio) m_audio->PlayPress(m_soundSet);
            SaveConfig();
            break;
        case kTraySoundSet:
            m_soundSet = m_soundSet == 1 ? 0 : 1;
            if (m_soundEnabled && m_audio) m_audio->PlayPress(m_soundSet);
            SaveConfig();
            break;
        case kTraySize: {
            int next = kSizePresets[0];
            for (const int preset : kSizePresets) {
                if (preset > m_size) {
                    next = preset;
                    break;
                }
            }
            ApplySize(next);
            break;
        }
        case kTrayAutoStart: {
            std::wstring error;
            if (!whale::SetAutoStart(!whale::IsAutoStartEnabled(), &error)) {
                DebugLog(L"autostart toggle failed: " + error);
            }
            break;
        }
        case kTraySettings:
            LaunchSettings();
            break;
        case kTrayQuotes:
            LaunchMode(L"--quotes");
            break;
        case kTrayTurnNotice:
            m_turnNotice = !m_turnNotice;
            SaveConfig();
            break;
        case kTrayAutoClose:
            m_policy.autoClose = !m_policy.autoClose;
            if (m_policy.autoClose && m_bubble != Bubble::Hidden) ArmHideTimer(CurrentKind());
            SaveConfig();
            break;
        case kTraySessionExit:
            // Closing the window leaves the supervisor alive, so the whale
            // comes back the next time Codex starts.
            DestroyWindow(m_hwnd);
            break;
        case kTrayExit:
            SignalSupervisorStop();
            DestroyWindow(m_hwnd);
            break;
        default:
            break;
        }
    }

    void LaunchMode(const wchar_t* mode) const {
        const auto exe = whale::CurrentExecutablePath();
        if (exe.empty()) return;
        std::wstring command = L"\"" + exe.wstring() + L"\" " + mode;
        STARTUPINFOW startup{sizeof(startup)};
        PROCESS_INFORMATION child{};
        if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                            nullptr, nullptr, &startup, &child)) {
            return;
        }
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
    }

    void LaunchSettings() const { LaunchMode(L"--settings"); }

    // Wakes the supervisor when the tray asks for a full exit; the supervisor
    // owns the event, so a manually started overlay simply finds nothing.
    void SignalSupervisorStop() const {
        const HANDLE stop = OpenEventW(EVENT_MODIFY_STATE, FALSE, whale::kSupervisorStopEvent);
        if (!stop) return;
        SetEvent(stop);
        CloseHandle(stop);
    }

    void ApplySize(int size) {
        const int wanted = std::clamp(size, 200, 900);
        if (wanted == m_size) return;
        m_size = wanted;
        m_unit = static_cast<float>(m_size) / kViewBox;
        if (!CreateCanvas()) return;
        SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, m_size, m_size, SWP_NOMOVE | SWP_NOACTIVATE);
        const auto work = WorkArea();
        RECT rect{};
        GetWindowRect(m_hwnd, &rect);
        const int x = std::clamp<int>(rect.left, work.left, work.right - m_size);
        const int y = std::clamp<int>(rect.top, work.top, work.bottom - m_size);
        SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
        if (m_bubble == Bubble::Quota) BuildQuotaLines();
        Render();
        Present();
        SaveConfig();
    }

    // Reapplies the on-disk configuration after the settings window saves it.
    void ReloadConfig() {
        LoadConfig();
        if (!m_options.hasPosition) {
            const auto work = WorkArea();
            SetWindowPos(m_hwnd, HWND_TOPMOST, work.right - m_size - 12, work.bottom - m_size - 12, 0, 0,
                         SWP_NOSIZE | SWP_NOACTIVATE);
        }
        ApplySize(m_options.size);
        if (m_bubble == Bubble::Quota) BuildQuotaLines();
        Render();
        Present();
    }

    bool InBubble(float x, float y) const {
        return PointInEllipse(x, y, kBubbleCx, kBubbleCy, kBubbleRx, kBubbleRy) ||
               PointInEllipse(x, y, kTail1Cx, kTail1Cy, kTail1Rx, kTail1Ry) ||
               PointInEllipse(x, y, kTail2Cx, kTail2Cy, kTail2Rx, kTail2Ry);
    }

    void HandleClick(POINT local, bool secondary) {
        if (secondary) {
            HideBubble();
            return;
        }
        const float x = static_cast<float>(local.x) / m_unit;
        const float y = static_cast<float>(local.y) / m_unit;
        if (m_bubble != Bubble::Hidden && InBubble(x, y)) {
            // Clicking the card turns the quota view into a random line and
            // dismisses every other scene, like the upstream widget.
            if (m_bubble == Bubble::Quota) ShowQuote();
            else HideBubble();
            return;
        }
        // The whale body always re-opens the quota card with a fresh timer,
        // even while the card is already on screen.
        ShowQuota();
    }

    void BeginDrag(POINT screen) {
        m_dragging = true;
        m_movedDuringPress = false;
        m_dragOrigin = screen;
        RECT rect{};
        GetWindowRect(m_hwnd, &rect);
        m_windowOrigin = POINT{rect.left, rect.top};
        SetCapture(m_hwnd);
    }

    void UpdateDrag(POINT screen) {
        if (!m_dragging) return;
        const int dx = screen.x - m_dragOrigin.x;
        const int dy = screen.y - m_dragOrigin.y;
        if (!m_movedDuringPress && std::abs(dx) <= kDragThreshold && std::abs(dy) <= kDragThreshold) return;
        m_movedDuringPress = true;
        SetWindowPos(m_hwnd, HWND_TOPMOST, m_windowOrigin.x + dx, m_windowOrigin.y + dy, 0, 0,
                     SWP_NOSIZE | SWP_NOACTIVATE);
    }

    void EndDrag(POINT screen, bool secondary) {
        if (!m_dragging) return;
        m_dragging = false;
        ReleaseCapture();
        if (m_movedDuringPress) {
            RECT rect{};
            GetWindowRect(m_hwnd, &rect);
            const auto work = WorkArea();
            int x = rect.left;
            int y = rect.top;
            if (std::abs(x - work.left) <= kSnapDistance) x = work.left;
            if (std::abs(y - work.top) <= kSnapDistance) y = work.top;
            if (std::abs((x + m_size) - work.right) <= kSnapDistance) x = work.right - m_size;
            if (std::abs((y + m_size) - work.bottom) <= kSnapDistance) y = work.bottom - m_size;
            SetWindowPos(m_hwnd, HWND_TOPMOST, x, y, 0, 0, SWP_NOSIZE | SWP_NOACTIVATE);
            SaveConfig();
            return;
        }
        // HandleClick tests the view box, so the release point has to be
        // mapped back to client space instead of passing screen coordinates.
        POINT local{screen.x, screen.y};
        ScreenToClient(m_hwnd, &local);
        HandleClick(local, secondary);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        if (m_reloadMessage != 0 && message == m_reloadMessage) {
            ReloadConfig();
            return 0;
        }
        switch (message) {
        case WM_NCHITTEST: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            POINT local = point;
            ScreenToClient(m_hwnd, &local);
            if (AlphaAt(local) < kTransparentHitAlpha) return HTTRANSPARENT;
            return HTCLIENT;
        }
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_LBUTTONDOWN: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(m_hwnd, &point);
            if (m_soundEnabled && m_audio) m_audio->PlayPress(m_soundSet);
            BeginDrag(point);
            return 0;
        }
        case WM_MOUSEMOVE: {
            if (m_dragging) {
                POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ClientToScreen(m_hwnd, &point);
                UpdateDrag(point);
                return 0;
            }
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            SetCursor(LoadCursorW(nullptr, IDC_HAND));
            return 0;
        }
        case WM_LBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(m_hwnd, &point);
            if (m_soundEnabled && m_audio) m_audio->PlayRelease(m_soundSet);
            EndDrag(point, false);
            return 0;
        }
        case WM_RBUTTONUP: {
            POINT point{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
            ClientToScreen(m_hwnd, &point);
            if (m_soundEnabled && m_audio) m_audio->PlayRelease(m_soundSet);
            EndDrag(point, true);
            return 0;
        }
        case kTrayCallbackMessage: {
            const UINT event = LOWORD(lParam);
            if (event == WM_RBUTTONUP || event == WM_CONTEXTMENU) {
                ShowTrayMenu();
            } else if (event == WM_LBUTTONDBLCLK) {
                ShowQuota();
            }
            return 0;
        }
        case kMessageShowQuota:
            ShowQuota();
            return 0;
        case kMessageSessionsChanged:
            // SetTimer with an existing id restarts the countdown, so a burst
            // of writes collapses into one refresh once the log goes quiet.
            SetTimer(m_hwnd, kIdSessionChange, kSessionDebounceMs, nullptr);
            return 0;
        case WM_HOTKEY:
            if (wParam == kHotkeyShowQuota) ToggleQuota();
            return 0;
        case WM_CAPTURECHANGED:
            m_dragging = false;
            return 0;
        case WM_TIMER:
            if (wParam == kIdHideBubble) {
                HideBubble();
            } else if (wParam == kIdRefresh) {
                if (m_bubble == Bubble::Quota) {
                    BuildQuotaLines();
                    Render();
                    Present();
                }
                RefreshAsync();
            } else if (wParam == kIdSessionChange) {
                KillTimer(m_hwnd, kIdSessionChange);
                RefreshAsync(true);
            }
            return 0;
        case kMessageSnapshot: {
            std::unique_ptr<UsageSnapshot> snapshot(reinterpret_cast<UsageSnapshot*>(lParam));
            if (snapshot) ApplySnapshot(*snapshot);
            return 0;
        }
        case WM_DESTROY:
            KillTimer(m_hwnd, kIdRefresh);
            KillTimer(m_hwnd, kIdHideBubble);
            KillTimer(m_hwnd, kIdSessionChange);
            if (m_watcher) m_watcher->Stop();
            if (m_hotkeyRegistered) UnregisterHotKey(m_hwnd, kHotkeyShowQuota);
            RemoveTray();
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }
    }

    OverlayOptions m_options;
    HWND m_hwnd{};
    HINSTANCE m_instance{};
    HANDLE m_instanceMutex{};
    UINT m_reloadMessage{};
    int m_size{440};
    float m_unit{};
    std::unique_ptr<Bitmap> m_canvas;
    HDC m_memoryDc{};
    HBITMAP m_dib{};
    void* m_dibBits{};
    std::unique_ptr<Image> m_whale;
    std::unique_ptr<FontFamily> m_family;
    std::unique_ptr<AudioPlayer> m_audio;
    std::unique_ptr<SessionWatcher> m_watcher;
    NOTIFYICONDATAW m_tray{};
    HICON m_trayIcon{};
    bool m_trayAdded{false};
    bool m_soundEnabled{true};
    int m_soundSet{0};
    BubblePolicy m_policy;
    bool m_turnNotice{true};
    bool m_hotkeyRegistered{false};
    std::wstring m_hotkeyLabel;
    Bubble m_bubble{Bubble::Hidden};
    std::vector<std::wstring> m_lines;
    std::vector<bool> m_staleLines;
    std::vector<float> m_lineWidths;
    UsageSnapshot m_snapshot;
    UsageMonitor m_monitor;
    std::vector<QuoteLine> m_quotes{DefaultQuotes()};
    std::size_t m_quoteIndex{(std::numeric_limits<std::size_t>::max)()};
    std::mt19937 m_generator{std::random_device{}()};
    bool m_hasSnapshot{false};
    std::atomic_bool m_scanInFlight{false};
    std::atomic_bool m_scanQueued{false};
    bool m_dragging{false};
    bool m_movedDuringPress{false};
    POINT m_dragOrigin{};
    POINT m_windowOrigin{};
};

} // namespace

int RunDesktopOverlay(const OverlayOptions& options) {
    GdiplusStartupInput input;
    ULONG_PTR token = 0;
    if (GdiplusStartup(&token, &input, nullptr) != Ok) return 1;
    int exitCode = 1;
    {
        OverlayWindow window(options);
        if (window.Create()) exitCode = window.Run();
    }
    GdiplusShutdown(token);
    return exitCode;
}

} // namespace whale
