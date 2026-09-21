#include "../core/usage_snapshot.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <iostream>

int main() {
    const auto root = std::filesystem::temp_directory_path() / "api-balance-whale-native-test";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root / "sessions" / "2026" / "09" / "20");
    const auto file = root / "sessions" / "2026" / "09" / "20" / "rollout.jsonl";
    std::ofstream out(file);
    out << R"({"type":"session_meta","timestamp":"2026-09-20T01:00:00Z","payload":{"model":"gpt-5.6-terra"}})" << '\n';
    out << R"({"type":"event_msg","timestamp":"2026-09-20T01:05:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":100,"output_tokens":20}},"rate_limits":{"primary":{"used_percent":12,"resets_at":1789707891},"secondary":{"used_percent":34,"resets_at":1789805325}}}})" << '\n';
    out << R"({"type":"event_msg","timestamp":"2026-09-20T01:06:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":160,"output_tokens":30}},"rate_limits":{"primary":{"used_percent":15,"resets_at":1789707891},"secondary":{"used_percent":35,"resets_at":1789805325}}}})" << '\n';
    out.close();

    const auto snapshot = whale::ReadUsageSnapshot(root, root / "state.json", std::chrono::system_clock::from_time_t(1789866420));
    assert(snapshot.todayTokens == 190);
    assert(snapshot.last7dTokens == 190);
    assert(snapshot.currentModel == "gpt-5.6-terra");
    assert(snapshot.fiveHour.has_value() && snapshot.fiveHour->usedPercent == 15);
    assert(snapshot.weekly.has_value() && snapshot.weekly->usedPercent == 35);
    assert(snapshot.lastTurn.has_value() && snapshot.lastTurn->deltaTokens == 70);

    std::filesystem::remove_all(root / "sessions");
    const auto fallback = whale::ReadUsageSnapshot(root, root / "state.json", std::chrono::system_clock::from_time_t(1789866440));
    assert(fallback.stale);
    assert(fallback.error == "读取失败，显示上次数据");
    assert(fallback.fiveHour.has_value() && fallback.fiveHour->usedPercent == 15);

    // A finished turn has to reach the overlay within a few seconds, so the
    // cache may not hide it behind a long TTL until the next poll happens.
    const auto live = std::filesystem::temp_directory_path() / "api-balance-whale-live-test";
    std::filesystem::remove_all(live);
    std::filesystem::create_directories(live / "sessions" / "2026" / "09" / "20");
    const auto state = live / "state.json";
    const auto rollout = live / "sessions" / "2026" / "09" / "20" / "rollout.jsonl";
    {
        std::ofstream out(rollout);
        out << R"({"type":"session_meta","timestamp":"2026-09-20T01:00:00Z","payload":{"model":"gpt-5.6-terra"}})" << '\n';
        out << R"({"type":"event_msg","timestamp":"2026-09-20T01:05:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":90,"output_tokens":30}},"rate_limits":{"primary":{"used_percent":12,"resets_at":1789707891},"secondary":{"used_percent":34,"resets_at":1789805325}}}})" << '\n';
    }
    const auto before = whale::ReadUsageSnapshot(live, state, std::chrono::system_clock::from_time_t(1789866420));
    assert(before.lastTurn.has_value() && before.lastTurn->deltaTokens == 120);

    {
        std::ofstream out(rollout, std::ios::app);
        out << R"({"type":"event_msg","timestamp":"2026-09-20T01:06:00Z","payload":{"type":"token_count","info":{"total_token_usage":{"input_tokens":150,"output_tokens":50}},"rate_limits":{"primary":{"used_percent":14,"resets_at":1789707891},"secondary":{"used_percent":36,"resets_at":1789805325}}}})" << '\n';
    }
    // Six seconds later, one poll interval, the new turn is already visible.
    const auto after = whale::ReadUsageSnapshot(live, state, std::chrono::system_clock::from_time_t(1789866426));
    assert(after.lastTurn.has_value() && after.lastTurn->deltaTokens == 80);

    // Inside the hot window the cached entry is reused, so polling does not
    // re-scan the sessions tree on every tick.
    std::filesystem::remove(rollout);
    const auto cached = whale::ReadUsageSnapshot(live, state, std::chrono::system_clock::from_time_t(1789866427));
    assert(!cached.stale && cached.lastTurn.has_value() && cached.lastTurn->deltaTokens == 80);

    // The directory watcher forces a probe, so its refresh never returns that
    // cached entry once the logs have moved on.
    const auto probed = whale::ReadUsageSnapshot(live, state, std::chrono::system_clock::from_time_t(1789866427), true);
    assert(probed.stale);
    std::filesystem::remove_all(live);
    std::cout << "native usage snapshot tests passed\n";
}
