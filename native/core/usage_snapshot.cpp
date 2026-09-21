#include "usage_snapshot.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <unordered_map>
#include <vector>

namespace whale {
namespace {

struct Event {
    std::int64_t at{};
    std::int64_t cumulative{};
    std::int64_t last{};
    std::string model;
    std::optional<UsageWindow> primary;
    std::optional<UsageWindow> secondary;
};

std::optional<std::string> StringField(const std::string& text, const std::string& key) {
    const auto marker = text.find('"' + key + '"');
    if (marker == std::string::npos) return std::nullopt;
    auto pos = text.find(':', marker + key.size() + 2);
    if (pos == std::string::npos) return std::nullopt;
    pos = text.find('"', pos + 1);
    if (pos == std::string::npos) return std::nullopt;
    const auto end = text.find('"', pos + 1);
    if (end == std::string::npos) return std::nullopt;
    return text.substr(pos + 1, end - pos - 1);
}

std::optional<double> NumberField(const std::string& text, const std::string& key) {
    const auto marker = text.find('"' + key + '"');
    if (marker == std::string::npos) return std::nullopt;
    auto pos = text.find(':', marker + key.size() + 2);
    if (pos == std::string::npos) return std::nullopt;
    ++pos;
    while (pos < text.size() && (text[pos] == ' ' || text[pos] == '\t')) ++pos;
    char* end = nullptr;
    const auto value = std::strtod(text.c_str() + pos, &end);
    if (end == text.c_str() + pos) return std::nullopt;
    return value;
}

std::int64_t TotalUsage(const std::string& text) {
    if (const auto total = NumberField(text, "total_tokens")) return std::max<std::int64_t>(0, static_cast<std::int64_t>(*total));
    std::int64_t sum = 0;
    for (const auto& key : {"input_tokens", "output_tokens", "reasoning_output_tokens"}) {
        if (const auto value = NumberField(text, key)) sum += std::max<std::int64_t>(0, static_cast<std::int64_t>(*value));
    }
    return sum;
}

std::int64_t Timestamp(const std::string& value) {
    std::tm tm{};
    std::istringstream stream(value.substr(0, 19));
    stream >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
    if (stream.fail()) return 0;
#ifdef _WIN32
    return static_cast<std::int64_t>(_mkgmtime(&tm));
#else
    return static_cast<std::int64_t>(timegm(&tm));
#endif
}

std::string Day(std::int64_t epochSeconds) {
    std::time_t raw = static_cast<std::time_t>(epochSeconds);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &raw);
#else
    gmtime_r(&raw, &tm);
#endif
    char buffer[11]{};
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%d", &tm);
    return buffer;
}

std::optional<UsageWindow> WindowField(const std::string& text, const std::string& key) {
    const auto marker = text.find('"' + key + '"');
    if (marker == std::string::npos) return std::nullopt;
    const auto end = text.find('}', marker);
    const auto part = text.substr(marker, end == std::string::npos ? std::string::npos : end - marker);
    const auto used = NumberField(part, "used_percent");
    const auto reset = NumberField(part, "resets_at");
    if (!used) return std::nullopt;
    return UsageWindow{*used, reset ? static_cast<std::int64_t>(*reset) : 0};
}

std::vector<std::filesystem::path> JsonlFiles(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> result;
    if (!std::filesystem::exists(root)) return result;
    const auto cutoff = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 8);
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".jsonl") continue;
        std::error_code error;
        const auto changed = entry.last_write_time(error);
        // A recently touched file can contain a long-running session, so keep it
        // even when its directory name is old. Unchanged older logs cannot affect
        // today's or the last-seven-day totals.
        if (!error && changed < cutoff) continue;
        result.push_back(entry.path());
    }
    std::sort(result.begin(), result.end());
    return result;
}

std::string Fingerprint(const std::vector<std::filesystem::path>& files) {
    std::ostringstream out;
    for (const auto& file : files) {
        std::error_code error;
        const auto size = std::filesystem::file_size(file, error);
        if (error) continue;
        const auto changed = std::filesystem::last_write_time(file, error).time_since_epoch().count();
        if (error) continue;
        out << file.u8string() << ':' << size << ':' << changed << '\n';
    }
    return std::to_string(std::hash<std::string>{}(out.str()));
}

// The cache keeps the overlay refresh timer and MCP calls from re-scanning the
// session directory. It has to stay well below the overlay refresh interval
// (kRefreshIntervalMs in desktop_overlay.cpp), otherwise a finished turn stays
// invisible until the entry expires and the bubble shows up seconds late.
constexpr std::int64_t kHotCacheSeconds = 2;

std::optional<UsageSnapshot> ReadCache(const std::filesystem::path& statePath, const std::string& fingerprint, const std::string& day, std::int64_t nowSeconds = 0) {
    std::ifstream input(statePath);
    if (!input) return std::nullopt;
    const std::string data{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (StringField(data, "day").value_or("") != day) return std::nullopt;
    if (!fingerprint.empty() && StringField(data, "fingerprint").value_or("") != fingerprint) return std::nullopt;
    if (fingerprint.empty() && NumberField(data, "generatedAt").value_or(0) + kHotCacheSeconds < nowSeconds) return std::nullopt;
    UsageSnapshot snapshot;
    snapshot.todayTokens = static_cast<std::int64_t>(NumberField(data, "todayTokens").value_or(0));
    snapshot.last7dTokens = static_cast<std::int64_t>(NumberField(data, "last7dTokens").value_or(0));
    snapshot.currentModel = StringField(data, "currentModel").value_or("");
    if (const auto fiveHour = NumberField(data, "fiveHourUsed")) snapshot.fiveHour = UsageWindow{*fiveHour, static_cast<std::int64_t>(NumberField(data, "fiveHourReset").value_or(0))};
    if (const auto weekly = NumberField(data, "weeklyUsed")) snapshot.weekly = UsageWindow{*weekly, static_cast<std::int64_t>(NumberField(data, "weeklyReset").value_or(0))};
    if (const auto delta = NumberField(data, "lastDelta")) {
        snapshot.lastTurn = LastTurn{StringField(data, "lastModel").value_or(""), static_cast<std::int64_t>(*delta), std::chrono::system_clock::from_time_t(static_cast<std::time_t>(NumberField(data, "lastTimestamp").value_or(0)))};
    }
    snapshot.stale = data.find("\"stale\":true") != std::string::npos;
    snapshot.error = StringField(data, "error").value_or("");
    return snapshot;
}

void WriteCache(const std::filesystem::path& statePath, const std::string& fingerprint, const std::string& day, std::int64_t nowSeconds, const UsageSnapshot& snapshot) {
    std::error_code error;
    std::filesystem::create_directories(statePath.parent_path(), error);
    std::ofstream output(statePath, std::ios::trunc);
    if (!output) return;
    output << "{\"version\":1,\"fingerprint\":\"" << fingerprint << "\",\"day\":\"" << day << "\",\"generatedAt\":" << nowSeconds
           << ",\"todayTokens\":" << snapshot.todayTokens << ",\"last7dTokens\":" << snapshot.last7dTokens
           << ",\"currentModel\":\"" << snapshot.currentModel << "\""
           << ",\"fiveHourUsed\":" << (snapshot.fiveHour ? std::to_string(snapshot.fiveHour->usedPercent) : "null")
           << ",\"fiveHourReset\":" << (snapshot.fiveHour ? std::to_string(snapshot.fiveHour->resetsAt) : "null")
           << ",\"weeklyUsed\":" << (snapshot.weekly ? std::to_string(snapshot.weekly->usedPercent) : "null")
           << ",\"weeklyReset\":" << (snapshot.weekly ? std::to_string(snapshot.weekly->resetsAt) : "null")
           << ",\"lastModel\":\"" << (snapshot.lastTurn ? snapshot.lastTurn->model : "") << "\""
           << ",\"lastDelta\":" << (snapshot.lastTurn ? std::to_string(snapshot.lastTurn->deltaTokens) : "null")
           << ",\"lastTimestamp\":" << (snapshot.lastTurn ? std::to_string(std::chrono::duration_cast<std::chrono::seconds>(snapshot.lastTurn->timestamp.time_since_epoch()).count()) : "null")
           << ",\"stale\":" << (snapshot.stale ? "true" : "false")
           << ",\"error\":\"" << snapshot.error << "\"}";
}

}

UsageSnapshot ReadUsageSnapshot(const std::filesystem::path& codexHome, const std::filesystem::path& statePath, std::chrono::system_clock::time_point now, bool forceProbe) {
    static std::optional<UsageSnapshot> lastGood;
    UsageSnapshot snapshot;
    std::unordered_map<std::string, std::int64_t> daily;
    std::int64_t newestAt = 0;
    std::int64_t newestRateAt = 0;
    std::int64_t nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    std::optional<UsageWindow> primary;
    std::optional<UsageWindow> secondary;

    const auto roots = {codexHome / "sessions", codexHome / "archived_sessions"};
    if (!forceProbe) {
        if (const auto cached = ReadCache(statePath, "", Day(nowSeconds), nowSeconds)) {
            if (!cached->stale) lastGood = *cached;
            return *cached;
        }
    }
    std::vector<std::filesystem::path> files;
    for (const auto& root : roots) {
        const auto found = JsonlFiles(root);
        files.insert(files.end(), found.begin(), found.end());
    }
    std::sort(files.begin(), files.end());
    const auto today = Day(nowSeconds);
    const auto fingerprint = Fingerprint(files);
    if (const auto cached = ReadCache(statePath, fingerprint, today)) {
        if (!cached->stale) lastGood = *cached;
        return *cached;
    }
    std::size_t fileCount = 0;
    for (const auto& file : files) {
        ++fileCount;
        std::ifstream input(file);
        std::string line;
        std::vector<Event> events;
        std::string model;
        while (std::getline(input, line)) {
            if (line.find("\"session_meta\"") != std::string::npos || line.find("\"turn_context\"") != std::string::npos) {
                if (const auto value = StringField(line, "model")) model = *value;
            }
            if (line.find("\"token_count\"") == std::string::npos || line.find("total_token_usage") == std::string::npos) continue;
            Event event;
            event.at = Timestamp(StringField(line, "timestamp").value_or(""));
            event.cumulative = TotalUsage(line);
            event.last = event.cumulative;
            event.model = model;
            event.primary = WindowField(line, "primary");
            event.secondary = WindowField(line, "secondary");
            events.push_back(event);
        }
        std::int64_t previous = 0;
        for (const auto& event : events) {
            const auto delta = event.cumulative >= previous ? event.cumulative - previous : event.last;
            daily[Day(event.at)] += delta;
            if (delta > 0 && event.at >= newestAt) {
                newestAt = event.at;
                snapshot.lastTurn = LastTurn{event.model, delta, std::chrono::system_clock::from_time_t(event.at)};
            }
            if (event.model.size() && event.at >= newestAt) snapshot.currentModel = event.model;
            if (event.at >= newestRateAt) {
                newestRateAt = event.at;
                if (event.primary) primary = event.primary;
                if (event.secondary) secondary = event.secondary;
            }
            previous = event.cumulative;
        }
    }
    snapshot.todayTokens = daily[today];
    for (const auto& [day, tokens] : daily) {
        std::tm tm{};
        std::istringstream stream(day + "T00:00:00");
        stream >> std::get_time(&tm, "%Y-%m-%dT%H:%M:%S");
#ifdef _WIN32
        const auto at = static_cast<std::int64_t>(_mkgmtime(&tm));
#else
        const auto at = static_cast<std::int64_t>(timegm(&tm));
#endif
        if (at >= nowSeconds - 7 * 24 * 60 * 60 && at <= nowSeconds) snapshot.last7dTokens += tokens;
    }
    snapshot.fiveHour = primary;
    snapshot.weekly = secondary;
    snapshot.stale = fileCount == 0 || (!primary && !secondary);
    if (fileCount == 0) snapshot.error = "暂无 Codex 会话日志";
    else if (snapshot.stale) snapshot.error = "暂无可用配额窗口";
    if (snapshot.stale && lastGood) {
        auto fallback = *lastGood;
        fallback.stale = true;
        fallback.error = "读取失败，显示上次数据";
        return fallback;
    }
    if (!snapshot.stale) lastGood = snapshot;
    WriteCache(statePath, fingerprint, today, nowSeconds, snapshot);
    return snapshot;
}

}
