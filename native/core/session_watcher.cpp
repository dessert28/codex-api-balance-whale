#include "session_watcher.hpp"

#include <windows.h>

#include <vector>

namespace whale {
namespace {

// Last write and size cover appends to a rollout log, the file name covers a
// brand new session file.
constexpr DWORD kWatchFilter = FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE | FILE_NOTIFY_CHANGE_FILE_NAME;
// A blocking ReadDirectoryChangesW could keep the thread stuck while the
// overlay shuts down, so every wait is bounded and the request is re-armed.
constexpr DWORD kWaitMs = 500;
constexpr DWORD kBufferBytes = 64 * 1024;

} // namespace

SessionWatcher::SessionWatcher(std::filesystem::path root, std::function<void()> onChanged)
    : m_root(std::move(root)), m_onChanged(std::move(onChanged)) {}

SessionWatcher::~SessionWatcher() { Stop(); }

void SessionWatcher::Start() {
    if (m_running.load()) return;
    std::error_code error;
    if (m_root.empty() || !std::filesystem::is_directory(m_root, error)) return;
    // FILE_FLAG_BACKUP_SEMANTICS is what lets CreateFileW open a directory, the
    // share flags keep Codex free to keep writing while we watch, and
    // FILE_FLAG_OVERLAPPED is required for the cancellable notification reads.
    const HANDLE directory = CreateFileW(m_root.c_str(), FILE_LIST_DIRECTORY,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OVERLAPPED, nullptr);
    if (directory == INVALID_HANDLE_VALUE) return;
    m_directory = directory;
    m_running.store(true);
    m_thread = std::thread([this] { Loop(); });
}

void SessionWatcher::Stop() {
    m_running.store(false);
    if (m_thread.joinable()) m_thread.join();
    if (m_directory) {
        CloseHandle(static_cast<HANDLE>(m_directory));
        m_directory = nullptr;
    }
}

void SessionWatcher::Loop() {
    const HANDLE directory = static_cast<HANDLE>(m_directory);
    std::vector<BYTE> buffer(kBufferBytes);
    OVERLAPPED overlapped{};
    overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!overlapped.hEvent) return;
    while (m_running.load()) {
        ResetEvent(overlapped.hEvent);
        if (!ReadDirectoryChangesW(directory, buffer.data(), static_cast<DWORD>(buffer.size()), TRUE,
                                   kWatchFilter, nullptr, &overlapped, nullptr)) {
            break;
        }
        if (WaitForSingleObject(overlapped.hEvent, kWaitMs) == WAIT_TIMEOUT) CancelIoEx(directory, &overlapped);
        DWORD bytes = 0;
        // Waiting for the result also drains a cancelled request, so the
        // OVERLAPPED never outlives this iteration.
        const BOOL finished = GetOverlappedResult(directory, &overlapped, &bytes, TRUE);
        const DWORD result = finished ? ERROR_SUCCESS : GetLastError();
        if (!m_running.load()) break;
        // A cancelled wait means nothing happened; an overflowed buffer still
        // means the logs moved on.
        if ((result == ERROR_SUCCESS || result == ERROR_NOTIFY_ENUM_DIR) && m_onChanged) m_onChanged();
    }
    CloseHandle(overlapped.hEvent);
}

} // namespace whale
