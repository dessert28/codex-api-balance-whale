#include "pch.h"
#include "core/supervisor.hpp"
#include "core/usage_snapshot.hpp"
#include "overlay/desktop_overlay.hpp"
#include "settings/quote_editor.hpp"
#include "settings/settings_window.hpp"

#include <shellapi.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <regex>
#include <sstream>
#include <string>

namespace {

void EnablePerMonitorDpiAwareness() {
    // Window and canvas coordinates are physical pixels, so the whale stays
    // crisp and lands exactly where it is dragged on scaled displays.
    const HMODULE user32 = LoadLibraryW(L"user32.dll");
    if (user32) {
        using SetProcessDpiAwarenessContextFn = BOOL(WINAPI*)(DPI_AWARENESS_CONTEXT);
        const auto setContext = reinterpret_cast<SetProcessDpiAwarenessContextFn>(
            GetProcAddress(user32, "SetProcessDpiAwarenessContext"));
        if (setContext && setContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2)) return;
    }
    SetProcessDPIAware();
}

std::string EscapeJson(const std::string& value) {
    std::ostringstream out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default: out << ch;
        }
    }
    return out.str();
}

std::string JsonString(const std::string& value) {
    return "\"" + EscapeJson(value) + "\"";
}

std::string WindowJson(const std::optional<whale::UsageWindow>& value) {
    if (!value) return "null";
    std::ostringstream out;
    out << "{\"usedPercent\":" << value->usedPercent << ",\"resetsAt\":" << value->resetsAt << "}";
    return out.str();
}

std::string SnapshotJson(const whale::UsageSnapshot& snapshot) {
    std::ostringstream out;
    out << "{\"ok\":true"
        << ",\"todayTokens\":" << snapshot.todayTokens
        << ",\"last7dTokens\":" << snapshot.last7dTokens
        << ",\"currentModel\":" << (snapshot.currentModel.empty() ? "null" : JsonString(snapshot.currentModel))
        << ",\"windows\":{\"fiveHour\":" << WindowJson(snapshot.fiveHour)
        << ",\"weekly\":" << WindowJson(snapshot.weekly) << "}"
        << ",\"lastTurn\":";
    if (snapshot.lastTurn) {
        const auto stamp = std::chrono::duration_cast<std::chrono::seconds>(snapshot.lastTurn->timestamp.time_since_epoch()).count();
        out << "{\"model\":" << (snapshot.lastTurn->model.empty() ? "null" : JsonString(snapshot.lastTurn->model))
            << ",\"deltaTokens\":" << snapshot.lastTurn->deltaTokens << ",\"timestamp\":" << stamp << "}";
    } else out << "null";
    out << ",\"stale\":" << (snapshot.stale ? "true" : "false")
        << ",\"error\":" << (snapshot.error.empty() ? "null" : JsonString(snapshot.error)) << "}";
    return out.str();
}

std::filesystem::path EnvironmentPath(const wchar_t* variable, std::filesystem::path fallback) {
    if (const auto value = _wgetenv(variable); value && *value) return value;
    return fallback;
}

std::filesystem::path CodexHome() {
    const auto profile = EnvironmentPath(L"USERPROFILE", L"C:\\Users\\Default");
    return EnvironmentPath(L"CODEX_HOME", profile / L".codex");
}

std::filesystem::path StatePath() {
    const auto local = EnvironmentPath(L"LOCALAPPDATA", L"C:\\Users\\Default\\AppData\\Local");
    return local / L"Codex" / L"api-balance-whale" / L"usage-cache.json";
}

std::filesystem::path OverlayConfigPath() {
    const auto local = EnvironmentPath(L"LOCALAPPDATA", L"C:\\Users\\Default\\AppData\\Local");
    return local / L"Codex" / L"api-balance-whale" / L"overlay.json";
}

std::filesystem::path WhaleAssetPath() {
    wchar_t image[MAX_PATH]{};
    GetModuleFileNameW(nullptr, image, MAX_PATH);
    const auto moduleDir = std::filesystem::path(image).parent_path();
    auto asset = moduleDir / L"assets" / L"DSniang1.png";
    if (!std::filesystem::exists(asset)) {
        asset = moduleDir.parent_path().parent_path() / L"assets" / L"DSniang1.png";
    }
    return asset;
}

std::string JsonId(const std::string& message) {
    std::smatch match;
    const std::regex pattern("\\\"id\\\"\\s*:\\s*(null|-?[0-9]+|\\\"(?:[^\\\"]|\\\\.)*\\\")");
    return std::regex_search(message, match, pattern) ? match[1].str() : "null";
}

std::string JsonMethod(const std::string& message) {
    std::smatch match;
    const std::regex pattern("\\\"method\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    return std::regex_search(message, match, pattern) ? match[1].str() : "";
}

std::string JsonField(const std::string& message, const char* key) {
    std::smatch match;
    const std::regex pattern(std::string("\\\"") + key + "\\\"\\s*:\\s*\\\"([^\\\"]+)\\\"");
    return std::regex_search(message, match, pattern) ? match[1].str() : "";
}

void Send(std::string_view id, const std::string& result) {
    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"result\":" << result << "}\n" << std::flush;
}

void SendError(std::string_view id, int code, std::string_view message) {
    std::cout << "{\"jsonrpc\":\"2.0\",\"id\":" << id << ",\"error\":{\"code\":" << code
              << ",\"message\":" << JsonString(std::string(message)) << "}}\n" << std::flush;
}

std::string ToolList() {
    return R"({"tools":[{"name":"whale_codex_usage","description":"读取本机 Codex/Work 的今日与近七天 token、五小时/周配额窗口及重置时间；只读会话日志，不读取密钥。","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true}},{"name":"whale_usage","description":"读取本机 Codex token 使用情况。","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true}},{"name":"whale_status","description":"读取小鲸鱼的本机配额监测状态。","inputSchema":{"type":"object","properties":{},"additionalProperties":false},"annotations":{"readOnlyHint":true}},{"name":"whale_open","description":"显示仅在 Codex 运行时存在的桌面小鲸鱼。","inputSchema":{"type":"object","properties":{},"additionalProperties":false}}]})";
}

bool CodexRunning();

std::string StatusJson() {
    std::ostringstream out;
    out << "{\"codexRunning\":" << (CodexRunning() ? "true" : "false")
        << ",\"native\":true,\"mode\":\"desktop-overlay\"}";
    return out.str();
}

bool StartOverlayProcess() {
    wchar_t image[MAX_PATH]{};
    GetModuleFileNameW(nullptr, image, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(image) + L"\" --overlay";
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child)) return false;
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return true;
}

void RunMcp() {
    std::string message;
    while (std::getline(std::cin, message)) {
        const auto id = JsonId(message);
        const auto method = JsonMethod(message);
        if (method == "initialize") {
            Send(id, R"({"protocolVersion":"2025-06-18","capabilities":{"tools":{"listChanged":false}},"serverInfo":{"name":"api-balance-whale","version":"0.3.0-native"}})");
        } else if (method == "ping") {
            Send(id, "{}");
        } else if (method == "tools/list") {
            Send(id, ToolList());
        } else if (method == "resources/list") {
            Send(id, R"({"resources":[]})");
        } else if (method == "resources/templates/list") {
            Send(id, R"({"resourceTemplates":[]})");
        } else if (method == "tools/call") {
            const auto tool = JsonField(message, "name");
            if (tool == "whale_codex_usage" || tool == "whale_usage") {
                const auto snapshot = whale::ReadUsageSnapshot(CodexHome(), StatePath());
                const auto output = SnapshotJson(snapshot);
                Send(id, "{\"content\":[{\"type\":\"text\",\"text\":" + JsonString(output) + "}],\"isError\":false}");
            } else if (tool == "whale_status") {
                Send(id, "{\"content\":[{\"type\":\"text\",\"text\":" + JsonString(StatusJson()) + "}],\"isError\":false}");
            } else if (tool == "whale_open") {
                const bool opened = StartOverlayProcess();
                Send(id, "{\"content\":[{\"type\":\"text\",\"text\":" + JsonString(opened ? "小鲸鱼已启动" : "小鲸鱼启动失败") + "}],\"isError\":" + std::string(opened ? "false" : "true") + "}");
            } else {
                SendError(id, -32602, "Unknown whale tool");
            }
        } else if (!method.empty()) {
            SendError(id, -32601, "Method not found");
        }
    }
}

bool CodexRunning() {
    const auto snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W process{sizeof(process)};
    bool found = false;
    if (Process32FirstW(snapshot, &process)) {
        do {
            const std::wstring name = process.szExeFile;
            if (_wcsicmp(name.c_str(), L"Codex.exe") == 0 || _wcsicmp(name.c_str(), L"Codex") == 0) { found = true; break; }
        } while (Process32NextW(snapshot, &process));
    }
    CloseHandle(snapshot);
    return found;
}

// Why an overlay process ended; the supervisor reacts differently to a user
// closed whale than to a crash.
enum class OverlayOutcome {
    SessionEnded,
    CodexExited,
    Crashed,
    Stopped,
};

bool SupervisorStopped(HANDLE stop) {
    return stop && WaitForSingleObject(stop, 0) == WAIT_OBJECT_0;
}

// Waits until Codex is running. False means the supervisor was asked to stop.
bool WaitForCodexRun(HANDLE stop) {
    while (!CodexRunning()) {
        if (SupervisorStopped(stop)) return false;
        Sleep(2000);
    }
    return true;
}

// "本次退出挂件" lasts until the next Codex start, so this first lets the
// current Codex session end before waiting for a new one.
bool WaitForNextCodexStart(HANDLE stop) {
    while (CodexRunning()) {
        if (SupervisorStopped(stop)) return false;
        Sleep(2000);
    }
    return WaitForCodexRun(stop);
}

bool LaunchOverlayChild(PROCESS_INFORMATION* child) {
    wchar_t image[MAX_PATH]{};
    GetModuleFileNameW(nullptr, image, MAX_PATH);
    std::wstring command = L"\"" + std::wstring(image) + L"\" --overlay";
    STARTUPINFOW startup{sizeof(startup)};
    return CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &startup, child) != FALSE;
}

// Keeps the overlay alive while Codex runs and reports why it ended.
OverlayOutcome BabysitOverlay(PROCESS_INFORMATION& child, HANDLE stop) {
    for (;;) {
        if (SupervisorStopped(stop)) {
            TerminateProcess(child.hProcess, 0);
            return OverlayOutcome::Stopped;
        }
        if (WaitForSingleObject(child.hProcess, 2000) == WAIT_OBJECT_0) break;
        if (!CodexRunning()) {
            TerminateProcess(child.hProcess, 0);
            return OverlayOutcome::CodexExited;
        }
    }
    DWORD code = 0;
    GetExitCodeProcess(child.hProcess, &code);
    return code == 0 ? OverlayOutcome::SessionEnded : OverlayOutcome::Crashed;
}

int RunSupervisor() {
    if (const auto console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);
    // install.ps1 (scheduled task) and the tray autostart toggle must never
    // run two supervisors, or two whales would appear.
    const HANDLE guard = CreateMutexW(nullptr, TRUE, L"Local\\ApiBalanceWhaleSupervisorInstance");
    if (guard && GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    // A full exit from the tray signals this event; a session exit leaves it
    // alone, so the supervisor keeps living and restores the whale the next
    // time Codex starts.
    const HANDLE stop = CreateEventW(nullptr, TRUE, FALSE, whale::kSupervisorStopEvent);
    if (stop && GetLastError() == ERROR_ALREADY_EXISTS) ResetEvent(stop);

    if (!WaitForCodexRun(stop)) return 0;
    int crashes = 0;
    for (;;) {
        PROCESS_INFORMATION child{};
        if (!LaunchOverlayChild(&child)) return 1;
        const DWORD startedAt = GetTickCount();
        const auto outcome = BabysitOverlay(child, stop);
        CloseHandle(child.hThread);
        CloseHandle(child.hProcess);
        if (outcome == OverlayOutcome::Stopped) return 0;
        if (outcome == OverlayOutcome::Crashed) {
            // A whale that dies right after launch would otherwise be
            // relaunched forever.
            if (GetTickCount() - startedAt > 5000) crashes = 0;
            if (++crashes > 3) return 1;
            Sleep(1000);
            if (!WaitForCodexRun(stop)) return 0;
            continue;
        }
        // Both a Codex shutdown and a session exit go dormant until the next
        // Codex start.
        if (!WaitForNextCodexStart(stop)) return 0;
        crashes = 0;
    }
}

int RunOverlay(bool settings = false) {
    if (const auto console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);
    if (settings) {
        whale::SettingsPaths paths;
        paths.configPath = OverlayConfigPath();
        paths.assetPath = WhaleAssetPath();
        return whale::RunSettingsWindow(paths);
    }
    whale::OverlayOptions options;
    options.codexHome = CodexHome();
    options.statePath = StatePath();
    options.configPath = OverlayConfigPath();
    options.assetPath = WhaleAssetPath();
    return whale::RunDesktopOverlay(options);
}

int RunQuoteEditor() {
    if (const auto console = GetConsoleWindow()) ShowWindow(console, SW_HIDE);
    whale::QuoteEditorPaths paths;
    paths.configPath = OverlayConfigPath();
    return whale::RunQuoteEditor(paths);
}

}

int wmain(int argc, wchar_t** argv) {
    EnablePerMonitorDpiAwareness();
    const std::wstring mode = argc > 1 ? argv[1] : L"--overlay";
    if (mode == L"--mcp") { RunMcp(); return 0; }
    if (mode == L"--supervisor") return RunSupervisor();
    if (mode == L"--settings") return RunOverlay(true);
    if (mode == L"--quotes") return RunQuoteEditor();
    return RunOverlay(false);
}
