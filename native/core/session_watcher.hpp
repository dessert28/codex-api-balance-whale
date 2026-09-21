#pragma once

#include <atomic>
#include <filesystem>
#include <functional>
#include <thread>

namespace whale {

// Watches the Codex sessions tree so a finished turn is picked up as soon as the
// log settles. Polling alone is several seconds late, which is why the upstream
// widget uses fs.watch on the rollout file. The callback runs on the watcher
// thread and must return quickly; the overlay only posts a message from it.
class SessionWatcher {
public:
    SessionWatcher(std::filesystem::path root, std::function<void()> onChanged);
    ~SessionWatcher();

    SessionWatcher(const SessionWatcher&) = delete;
    SessionWatcher& operator=(const SessionWatcher&) = delete;

    // No-op when the root does not exist yet; callers can retry later.
    void Start();
    void Stop();

private:
    void Loop();

    std::filesystem::path m_root;
    std::function<void()> m_onChanged;
    std::thread m_thread;
    void* m_directory{};
    std::atomic_bool m_running{false};
};

} // namespace whale
