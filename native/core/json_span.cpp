#include "json_span.hpp"

namespace whale {

bool FindJsonValueSpan(const std::string& text, const char* key, char open, char close,
                       std::size_t& begin, std::size_t& end) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto keyAt = text.find(needle);
    if (keyAt == std::string::npos) return false;
    const auto valueAt = text.find(open, keyAt + needle.size());
    if (valueAt == std::string::npos) return false;
    int depth = 0;
    bool inString = false;
    for (std::size_t index = valueAt; index < text.size(); ++index) {
        const char ch = text[index];
        if (inString) {
            if (ch == '\\') ++index;
            else if (ch == '"') inString = false;
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == open) {
            ++depth;
        } else if (ch == close) {
            if (--depth == 0) {
                begin = valueAt;
                end = index + 1;
                return true;
            }
        }
    }
    return false;
}

std::string SetJsonValue(const std::string& text, const char* key, const std::string& value) {
    if (value.empty()) return text;
    const char open = value.front();
    const char close = open == '[' ? ']' : '}';
    std::size_t begin = 0;
    std::size_t end = 0;
    if (FindJsonValueSpan(text, key, open, close, begin, end)) {
        return text.substr(0, begin) + value + text.substr(end);
    }
    const auto closing = text.rfind('}');
    if (closing == std::string::npos) return text;
    std::size_t insertAt = closing;
    while (insertAt > 0 && (text[insertAt - 1] == ' ' || text[insertAt - 1] == '\n' ||
                            text[insertAt - 1] == '\r' || text[insertAt - 1] == '\t')) {
        --insertAt;
    }
    const std::string entry = "\"" + std::string(key) + "\":" + value;
    if (insertAt > 0 && text[insertAt - 1] == '{') {
        return text.substr(0, insertAt) + entry + text.substr(insertAt);
    }
    return text.substr(0, insertAt) + ',' + entry + text.substr(insertAt);
}

} // namespace whale
