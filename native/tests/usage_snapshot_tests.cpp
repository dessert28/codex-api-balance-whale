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
    std::cout << "native usage snapshot tests passed\n";
}
