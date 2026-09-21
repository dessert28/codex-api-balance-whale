#include "pch.h"
#include "autostart.hpp"

#include <sstream>

namespace whale {
namespace {

constexpr wchar_t kRunKey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";

std::wstring Quote(const std::filesystem::path& path) {
    return L"\"" + path.wstring() + L"\"";
}

bool RunTaskScheduler(const std::wstring& arguments, bool expectSuccess) {
    wchar_t system32[MAX_PATH]{};
    if (!GetSystemDirectoryW(system32, MAX_PATH)) return false;
    const auto schtasks = std::filesystem::path(system32) / L"schtasks.exe";
    std::wstring command = Quote(schtasks) + L" " + arguments;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION child{};
    if (!CreateProcessW(nullptr, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
                        nullptr, nullptr, &startup, &child)) {
        return false;
    }
    WaitForSingleObject(child.hProcess, 15000);
    DWORD code = 1;
    GetExitCodeProcess(child.hProcess, &code);
    CloseHandle(child.hThread);
    CloseHandle(child.hProcess);
    return expectSuccess ? code == 0 : true;
}

bool ScheduledTaskExists() {
    std::wstring query = L"/Query /TN \"" + std::wstring(kScheduledTaskName) + L"\"";
    return RunTaskScheduler(query, true);
}

} // namespace

const wchar_t* const kScheduledTaskName = L"Codex API Balance Whale";
const wchar_t* const kRunValueName = L"ApiBalanceWhale";

std::filesystem::path CurrentExecutablePath() {
    wchar_t buffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameW(nullptr, buffer, MAX_PATH);
    if (length == 0) return {};
    return std::filesystem::path(std::wstring(buffer, length));
}

bool IsAutoStartEnabled() {
    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_QUERY_VALUE, &key) == ERROR_SUCCESS) {
        wchar_t value[1024]{};
        DWORD size = sizeof(value);
        const auto status = RegQueryValueExW(key, kRunValueName, nullptr, nullptr,
                                             reinterpret_cast<LPBYTE>(value), &size);
        RegCloseKey(key);
        if (status == ERROR_SUCCESS && size > 0) return true;
    }
    return ScheduledTaskExists();
}

bool SetAutoStart(bool enable, std::wstring* error) {
    if (enable) {
        const auto exe = CurrentExecutablePath();
        if (exe.empty()) {
            if (error) *error = L"无法定位 api-balance-whale.exe";
            return false;
        }
        HKEY key{};
        const auto status = RegCreateKeyExW(HKEY_CURRENT_USER, kRunKey, 0, nullptr, 0,
                                            KEY_SET_VALUE, nullptr, &key, nullptr);
        if (status != ERROR_SUCCESS) {
            if (error) *error = L"无法写入当前用户启动项（注册表 Run 键）";
            return false;
        }
        const std::wstring command = Quote(exe) + L" --supervisor";
        const auto written = RegSetValueExW(key, kRunValueName, 0, REG_SZ,
                                            reinterpret_cast<const BYTE*>(command.c_str()),
                                            static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (written != ERROR_SUCCESS) {
            if (error) *error = L"写入启动项失败";
            return false;
        }
        // Keep one autostart mechanism only.
        RunTaskScheduler(L"/Delete /TN \"" + std::wstring(kScheduledTaskName) + L"\" /F", false);
        return true;
    }

    HKEY key{};
    if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &key) == ERROR_SUCCESS) {
        RegDeleteValueW(key, kRunValueName);
        RegCloseKey(key);
    }
    RunTaskScheduler(L"/Delete /TN \"" + std::wstring(kScheduledTaskName) + L"\" /F", false);
    return true;
}

} // namespace whale
