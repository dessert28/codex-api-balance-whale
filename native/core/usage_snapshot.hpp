#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>

namespace whale {

struct UsageWindow {
    double usedPercent{};
    std::int64_t resetsAt{};
};

struct LastTurn {
    std::string model;
    std::int64_t deltaTokens{};
    std::chrono::system_clock::time_point timestamp{};
};

struct UsageSnapshot {
    std::int64_t todayTokens{};
    std::int64_t last7dTokens{};
    std::string currentModel;
    std::optional<UsageWindow> fiveHour;
    std::optional<UsageWindow> weekly;
    std::optional<LastTurn> lastTurn;
    bool stale{true};
    std::string error;
};

// forceProbe skips the short-lived cache; callers that already know the session
// logs changed (the overlay watches the sessions directory) use it so a finished
// turn shows up right away instead of waiting for the cache to expire.
UsageSnapshot ReadUsageSnapshot(
    const std::filesystem::path& codexHome,
    const std::filesystem::path& statePath,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now(),
    bool forceProbe = false);

}
