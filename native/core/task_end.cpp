#include "task_end.hpp"

#include "json_span.hpp"

#include <cstdlib>
#include <string>

namespace whale {
namespace {

// Keep this in sync with the upstream sound picker: it offers the duck and the
// built-in effect pair, each with a press and a release clip.
constexpr const char* kPresets[kTaskEndPresetCount] = {
    "preset:duck:press", "preset:duck:release", "preset:fx1:press", "preset:fx1:release"};

constexpr const char* kDefaultPreset = "preset:duck:press";

std::string EscapeJson(const std::string& value) {
    std::string out;
    out.reserve(value.size());
    for (const char ch : value) {
        switch (ch) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string ReadJsonString(const std::string& text, const char* key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto keyAt = text.find(needle);
    if (keyAt == std::string::npos) return {};
    const auto open = text.find('"', keyAt + needle.size());
    if (open == std::string::npos) return {};
    std::string out;
    for (std::size_t index = open + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '\\' && index + 1 < text.size()) {
            const char next = text[++index];
            out.push_back(next == 'n' ? '\n' : next == 'r' ? '\r' : next == 't' ? '\t' : next);
        } else if (ch == '"') {
            return out;
        } else {
            out.push_back(ch);
        }
    }
    return out;
}

bool ReadJsonBool(const std::string& text, const char* key, bool fallback) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto keyAt = text.find(needle);
    if (keyAt == std::string::npos) return fallback;
    const auto valueAt = text.find_first_not_of(" \t\r\n:", keyAt + needle.size());
    if (valueAt == std::string::npos) return fallback;
    if (text.compare(valueAt, 4, "true") == 0) return true;
    if (text.compare(valueAt, 5, "false") == 0) return false;
    // Upstream stores a boolean; older files (and our flat config) may use 0/1.
    return text[valueAt] != '0';
}

} // namespace

const char* TaskEndPresetAt(int index) {
    if (index < 0 || index >= kTaskEndPresetCount) return kDefaultPreset;
    return kPresets[index];
}

const char* TaskEndDefaultPreset() {
    return kDefaultPreset;
}

const char* TaskEndEffectivePreset(const std::string& sel) {
    for (const char* preset : kPresets) {
        if (sel == preset) return preset;
    }
    return kDefaultPreset;
}

bool ParseTaskEndPreset(const std::string& sel, int& set, bool& press) {
    if (sel == "preset:duck:press" || sel == "preset:duck:release") {
        set = 1;
        press = sel == "preset:duck:press";
        return true;
    }
    if (sel == "preset:fx1:press" || sel == "preset:fx1:release") {
        set = 0;
        press = sel == "preset:fx1:press";
        return true;
    }
    return false;
}

std::wstring TaskEndPresetLabel(const std::string& sel) {
    const std::string id = TaskEndEffectivePreset(sel);
    std::wstring label;
    label += id.find(":duck:") != std::string::npos ? L"小黄鸭" : L"音效1";
    label += id.rfind(":press") != std::string::npos ? L"·按下" : L"·松开";
    return label;
}

std::string NextTaskEndPreset(const std::string& sel) {
    const std::string current = TaskEndEffectivePreset(sel);
    for (int index = 0; index < kTaskEndPresetCount; ++index) {
        if (current == kPresets[index]) return kPresets[(index + 1) % kTaskEndPresetCount];
    }
    return kDefaultPreset;
}

bool FindTaskEndSpan(const std::string& configText, std::size_t& begin, std::size_t& end) {
    return FindJsonValueSpan(configText, "taskEnd", '{', '}', begin, end);
}

TaskEndSound ParseTaskEndJson(const std::string& configText) {
    TaskEndSound value;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!FindTaskEndSpan(configText, begin, end)) return value;
    const auto object = configText.substr(begin, end - begin);
    value.on = ReadJsonBool(object, "on", value.on);
    value.sel = ReadJsonString(object, "sel");
    return value;
}

std::string TaskEndJson(const TaskEndSound& value) {
    return std::string("{\"on\":") + (value.on ? "1" : "0") + ",\"sel\":\"" + EscapeJson(value.sel) + "\"}";
}

std::string SetTaskEndJson(const std::string& configText, const TaskEndSound& value) {
    return SetJsonValue(configText, "taskEnd", TaskEndJson(value));
}

} // namespace whale
