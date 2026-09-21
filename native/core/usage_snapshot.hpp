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

UsageSnapshot ReadUsageSnapshot(
    const std::filesystem::path& codexHome,
    const std::filesystem::path& statePath,
    std::chrono::system_clock::time_point now = std::chrono::system_clock::now());

}

