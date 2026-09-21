#include "quote_editor.hpp"

#include "../core/quotes.hpp"
#include "../overlay/desktop_overlay.hpp"

#include <windows.h>

#include <fstream>
#include <iterator>
#include <string>
#include <utility>
#include <vector>

namespace whale {
namespace {

constexpr wchar_t kQuoteEditorClass[] = L"ApiBalanceWhaleQuoteEditor";
constexpr wchar_t kQuoteEditorTitle[] = L"Codex 配额小鲸鱼 · 随机语句";
constexpr wchar_t kQuoteHint[] =
    L"每行一条随机语句，写成「权重|文本」，例如 3|今天也要好好休息呀～\r\n"
    L"省略权重就是 1，空行忽略；权重范围 1-99。";

constexpr int kIdHint = 2101;
constexpr int kIdEdit = 2102;
constexpr int kIdDefaults = 2103;
constexpr int kIdCancel = 2104;
constexpr int kIdSave = 2105;
constexpr int kMinWidth = 440;
constexpr int kMinHeight = 340;

std::string ReadAll(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return {};
    return std::string((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
}

class QuoteEditor {
public:
    explicit QuoteEditor(QuoteEditorPaths paths) : m_paths(std::move(paths)) {}

    bool Create() {
        WNDCLASSEXW windowClass{sizeof(windowClass)};
        windowClass.lpfnWndProc = &QuoteEditor::StaticProc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kQuoteEditorClass;
        windowClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        windowClass.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
        RegisterClassExW(&windowClass);
        m_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW, kQuoteEditorClass, kQuoteEditorTitle,
                                 WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX | WS_CLIPCHILDREN,
                                 CW_USEDEFAULT, CW_USEDEFAULT, kMinWidth, kMinHeight,
                                 nullptr, nullptr, windowClass.hInstance, this);
        if (!m_hwnd) return false;
        CenterWindow(kMinWidth, kMinHeight);
        ShowWindow(m_hwnd, SW_SHOW);
        UpdateWindow(m_hwnd);
        return true;
    }

    int Run() {
        MSG message{};
        while (GetMessageW(&message, nullptr, 0, 0) > 0) {
            if (!IsDialogMessageW(m_hwnd, &message)) {
                TranslateMessage(&message);
                DispatchMessageW(&message);
            }
        }
        return m_exitCode;
    }

private:
    static LRESULT CALLBACK StaticProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam) {
        QuoteEditor* self = reinterpret_cast<QuoteEditor*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (message == WM_NCCREATE) {
            const auto created = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<QuoteEditor*>(created->lpCreateParams);
            self->m_hwnd = handle;
            SetWindowLongPtrW(handle, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        }
        if (!self) return DefWindowProcW(handle, message, wParam, lParam);
        return self->HandleMessage(message, wParam, lParam);
    }

    LRESULT HandleMessage(UINT message, WPARAM wParam, LPARAM lParam) {
        switch (message) {
        case WM_CREATE:
            CreateChildren();
            return 0;
        case WM_SIZE:
            Layout();
            return 0;
        case WM_GETMINMAXINFO: {
            const auto scale = Scale();
            auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
            info->ptMinTrackSize.x = static_cast<LONG>(kMinWidth * scale);
            info->ptMinTrackSize.y = static_cast<LONG>(kMinHeight * scale);
            return 0;
        }
        case WM_COMMAND:
            switch (LOWORD(wParam)) {
            case kIdSave:
            case IDOK:
                Save();
                return 0;
            case kIdDefaults:
                SetWindowTextW(m_edit, FormatQuoteText(DefaultQuotes()).c_str());
                return 0;
            case kIdCancel:
            case IDCANCEL:
                DestroyWindow(m_hwnd);
                return 0;
            default:
                return 0;
            }
        case WM_CLOSE:
            DestroyWindow(m_hwnd);
            return 0;
        case WM_DESTROY:
            if (m_font) DeleteObject(m_font);
            PostQuitMessage(0);
            return 0;
        default:
            return DefWindowProcW(m_hwnd, message, wParam, lParam);
        }
    }

    // Physical pixels, like the rest of the overlay: the process is per monitor
    // aware, so the layout is scaled by the window DPI once.
    double Scale() const {
        const HDC dc = GetDC(m_hwnd);
        const int dpi = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (dc) ReleaseDC(m_hwnd, dc);
        return dpi > 0 ? static_cast<double>(dpi) / 96.0 : 1.0;
    }

    void CenterWindow(int width, int height) {
        RECT area{};
        SystemParametersInfoW(SPI_GETWORKAREA, 0, &area, 0);
        RECT rect{0, 0, width, height};
        AdjustWindowRectEx(&rect, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_SIZEBOX, FALSE, WS_EX_TOOLWINDOW);
        const int w = rect.right - rect.left;
        const int h = rect.bottom - rect.top;
        SetWindowPos(m_hwnd, nullptr, area.left + ((area.right - area.left) - w) / 2,
                     area.top + ((area.bottom - area.top) - h) / 2, w, h, SWP_NOZORDER | SWP_NOACTIVATE);
    }

    void CreateChildren() {
        const auto scale = Scale();
        const int fontHeight = -static_cast<int>(15 * scale);
        m_font = CreateFontW(fontHeight, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH,
                             L"Microsoft YaHei UI");
        m_hint = CreateWindowExW(0, L"STATIC", kQuoteHint, WS_CHILD | WS_VISIBLE | SS_LEFT,
                                 0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdHint)), nullptr, nullptr);
        m_edit = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL | ES_WANTRETURN,
                                 0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdEdit)), nullptr, nullptr);
        m_defaults = CreateWindowExW(0, L"BUTTON", L"恢复默认", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                     0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdDefaults)), nullptr, nullptr);
        m_cancel = CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                                   0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdCancel)), nullptr, nullptr);
        m_save = CreateWindowExW(0, L"BUTTON", L"保存", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                                 0, 0, 0, 0, m_hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(kIdSave)), nullptr, nullptr);
        for (const HWND child : {m_hint, m_edit, m_defaults, m_cancel, m_save}) {
            if (child && m_font) SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(m_font), TRUE);
        }
        SetWindowTextW(m_edit, FormatQuoteText(CurrentLines()).c_str());
        Layout();
        SetFocus(m_edit);
    }

    std::vector<QuoteLine> CurrentLines() const {
        const auto lines = ParseQuoteJson(ReadAll(m_paths.configPath));
        return lines.empty() ? DefaultQuotes() : lines;
    }

    void Layout() {
        if (!m_edit) return;
        RECT client{};
        GetClientRect(m_hwnd, &client);
        const auto scale = Scale();
        const int margin = static_cast<int>(14 * scale);
        const int gap = static_cast<int>(8 * scale);
        const int buttonWidth = static_cast<int>(92 * scale);
        const int buttonHeight = static_cast<int>(30 * scale);
        const int hintHeight = static_cast<int>(38 * scale);
        const int width = client.right - client.left;
        const int height = client.bottom - client.top;
        MoveWindow(m_hint, margin, margin, width - 2 * margin, hintHeight, TRUE);
        const int editTop = margin + hintHeight + gap;
        const int buttonsTop = height - margin - buttonHeight;
        MoveWindow(m_edit, margin, editTop, width - 2 * margin, buttonsTop - gap - editTop, TRUE);
        MoveWindow(m_defaults, margin, buttonsTop, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_save, width - margin - buttonWidth, buttonsTop, buttonWidth, buttonHeight, TRUE);
        MoveWindow(m_cancel, width - margin - 2 * buttonWidth - gap, buttonsTop, buttonWidth, buttonHeight, TRUE);
    }

    std::wstring ReadEditText() const {
        const int length = GetWindowTextLengthW(m_edit);
        if (length <= 0) return {};
        std::wstring text(static_cast<std::size_t>(length) + 1, L'\0');
        GetWindowTextW(m_edit, text.data(), length + 1);
        text.resize(static_cast<std::size_t>(length));
        return text;
    }

    void Save() {
        const auto lines = ParseQuoteText(ReadEditText());
        if (lines.empty()) {
            MessageBoxW(m_hwnd, L"至少保留一条随机语句。", kQuoteEditorTitle, MB_OK | MB_ICONINFORMATION);
            SetFocus(m_edit);
            return;
        }
        auto config = ReadAll(m_paths.configPath);
        if (config.empty()) config = "{}";
        const auto updated = SetQuoteJson(config, lines);
        std::error_code error;
        std::filesystem::create_directories(m_paths.configPath.parent_path(), error);
        std::ofstream output(m_paths.configPath, std::ios::binary | std::ios::trunc);
        if (!output) {
            MessageBoxW(m_hwnd, (L"无法写入 " + m_paths.configPath.wstring()).c_str(), kQuoteEditorTitle, MB_OK | MB_ICONERROR);
            return;
        }
        output << updated;
        output.close();
        NotifyOverlay();
        DestroyWindow(m_hwnd);
    }

    void NotifyOverlay() const {
        const HWND overlay = FindWindowW(kOverlayWindowClass, nullptr);
        if (!overlay) return;
        const UINT reload = RegisterWindowMessageW(kOverlayReloadMessage);
        if (reload) SendMessageW(overlay, reload, 0, 0);
    }

    QuoteEditorPaths m_paths;
    HWND m_hwnd{};
    HWND m_hint{};
    HWND m_edit{};
    HWND m_defaults{};
    HWND m_cancel{};
    HWND m_save{};
    HFONT m_font{};
    int m_exitCode{};
};

} // namespace

int RunQuoteEditor(const QuoteEditorPaths& paths) {
    QuoteEditor editor(paths);
    if (!editor.Create()) return 1;
    return editor.Run();
}

} // namespace whale
