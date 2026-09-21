#include "../core/session_watcher.hpp"

#include <cassert>
#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "api-balance-whale-watcher-test";
    std::filesystem::remove_all(root);
    const auto sessions = root / "sessions";
    std::filesystem::create_directories(sessions / "2026" / "09" / "21");

    std::mutex mutex;
    std::condition_variable signal;
    int notifications = 0;
    whale::SessionWatcher watcher(sessions, [&] {
        {
            std::lock_guard<std::mutex> lock(mutex);
            ++notifications;
        }
        signal.notify_all();
    });
    watcher.Start();

    // A missing root is ignored instead of throwing.
    whale::SessionWatcher missing(root / "does-not-exist", [] {});
    missing.Start();
    missing.Stop();

    bool seen = false;
    // A directory change is only reported while a read is pending, so the test
    // keeps writing until the notification shows up. The file lives in a nested
    // directory because that is where Codex writes its sessions.
    for (int attempt = 0; attempt < 20 && !seen; ++attempt) {
        {
            std::ofstream out(sessions / "2026" / "09" / "21" / "rollout.jsonl", std::ios::app);
            out << "{}" << '\n';
        }
        std::unique_lock<std::mutex> lock(mutex);
        seen = signal.wait_for(lock, std::chrono::milliseconds(250), [&] { return notifications > 0; });
    }
    watcher.Stop();
    assert(seen);

    // Stop is idempotent and safe after the watcher was never started.
    watcher.Stop();
    whale::SessionWatcher unused(sessions, [] {});
    unused.Stop();

    std::filesystem::remove_all(root);
    std::cout << "native session watcher tests passed (" << notifications << " notifications)" << '\n';
}
