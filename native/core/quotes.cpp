#include "json_span.hpp"
#include "quotes.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

#include <windows.h>

namespace whale {
namespace {

// Upstream retries a few times when the roll lands on the line it wants to avoid.
constexpr int kMaxPickTries = 6;

std::string WideToUtf8(const std::wstring& value) {
    if (value.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length, nullptr, nullptr);
    return result;
}

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring TrimWide(const std::wstring& value) {
    const auto first = value.find_first_not_of(L" \t\r\n");
    if (first == std::wstring::npos) return {};
    const auto last = value.find_last_not_of(L" \t\r\n");
    return value.substr(first, last - first + 1);
}

void AppendCodePoint(std::string& out, unsigned int codePoint) {
    if (codePoint <= 0x7F) {
        out += static_cast<char>(codePoint);
    } else if (codePoint <= 0x7FF) {
        out += static_cast<char>(0xC0 | (codePoint >> 6));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else if (codePoint <= 0xFFFF) {
        out += static_cast<char>(0xE0 | (codePoint >> 12));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (codePoint >> 18));
        out += static_cast<char>(0x80 | ((codePoint >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((codePoint >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (codePoint & 0x3F));
    }
}

int HexDigit(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

bool ReadHex4(const std::string& text, std::size_t at, unsigned int& value) {
    if (at + 4 > text.size()) return false;
    value = 0;
    for (std::size_t offset = 0; offset < 4; ++offset) {
        const int digit = HexDigit(text[at + offset]);
        if (digit < 0) return false;
        value = (value << 4) | static_cast<unsigned int>(digit);
    }
    return true;
}

// Writes the escaped body of a JSON string, so quotes and line breaks survive a
// hand edited config next to plain UTF-8 text.
std::string EscapeJsonBody(const std::wstring& text) {
    const auto utf8 = WideToUtf8(text);
    std::string out;
    out.reserve(utf8.size() + 8);
    for (const char ch : utf8) {
        switch (ch) {
        case '\\': out += "\\\\"; break;
        case '"': out += "\\\""; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                char buffer[8]{};
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", static_cast<unsigned char>(ch));
                out += buffer;
            } else {
                out += ch;
            }
        }
    }
    return out;
}

// Parses a JSON string whose opening quote sits at `start`.
bool ReadJsonString(const std::string& text, std::size_t start, std::string& value, std::size_t& end) {
    if (start >= text.size() || text[start] != '"') return false;
    std::string out;
    for (std::size_t index = start + 1; index < text.size(); ++index) {
        const char ch = text[index];
        if (ch == '"') {
            value = out;
            end = index + 1;
            return true;
        }
        if (ch != '\\') {
            out += ch;
            continue;
        }
        if (index + 1 >= text.size()) return false;
        const char escape = text[++index];
        switch (escape) {
        case 'n': out += '\n'; break;
        case 'r': out += '\r'; break;
        case 't': out += '\t'; break;
        case 'b': out += '\b'; break;
        case 'f': out += '\f'; break;
        case '/': out += '/'; break;
        case '"': out += '"'; break;
        case '\\': out += '\\'; break;
        case 'u': {
            unsigned int code = 0;
            if (!ReadHex4(text, index + 1, code)) return false;
            index += 4;
            if (code >= 0xD800 && code <= 0xDBFF && index + 6 < text.size() && text[index + 1] == '\\' && text[index + 2] == 'u') {
                unsigned int low = 0;
                if (ReadHex4(text, index + 3, low) && low >= 0xDC00 && low <= 0xDFFF) {
                    index += 6;
                    code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                }
            }
            AppendCodePoint(out, code);
            break;
        }
        default: return false;
        }
    }
    return false;
}

int NumberAfter(const std::string& object, const char* key, int fallback) {
    const auto keyAt = object.find(std::string("\"") + key + "\"");
    if (keyAt == std::string::npos) return fallback;
    const auto valueAt = object.find(':', keyAt + std::strlen(key) + 2);
    if (valueAt == std::string::npos) return fallback;
    return std::atoi(object.c_str() + valueAt + 1);
}

int ClampWeight(int weight) { return std::clamp(weight, 1, kMaxQuoteWeight); }

// Finds `wanted` after `from`, skipping everything inside JSON strings, so a line
// whose text contains braces (the upstream default is `{balance_api}`) cannot be
// mistaken for object structure.
std::size_t FindOutsideString(const std::string& text, char wanted, std::size_t from, std::size_t limit) {
    bool inString = false;
    for (std::size_t index = from; index < limit && index < text.size(); ++index) {
        const char ch = text[index];
        if (inString) {
            if (ch == '\\') {
                ++index;
            } else if (ch == '"') {
                inString = false;
            }
            continue;
        }
        if (ch == '"') {
            inString = true;
        } else if (ch == wanted) {
            return index;
        }
    }
    return std::string::npos;
}

} // namespace

std::vector<QuoteLine> DefaultQuotes() {
    static const wchar_t* const kDefaults[] = {
        L"今天也要好好休息呀～",
        L"鲸鱼正在帮你看配额～",
        L"慢一点，思路会更清楚～",
        L"本轮完成，继续保持～",
        L"喝口水，再写一段～",
        L"配额在这里，安心工作吧～",
        L"写代码也要记得眨眼哦～",
        L"再忙也别忘记吃饭～",
        L"今天的你依旧很可靠～",
        L"深呼吸，然后继续～"};
    std::vector<QuoteLine> lines;
    lines.reserve(std::size(kDefaults));
    for (const auto* text : kDefaults) lines.push_back(QuoteLine{text, 1});
    return lines;
}

std::vector<QuoteLine> ParseQuoteText(const std::wstring& text) {
    std::vector<QuoteLine> lines;
    std::wistringstream stream(text);
    std::wstring row;
    while (std::getline(stream, row)) {
        auto trimmed = TrimWide(row);
        if (trimmed.empty()) continue;
        int weight = 1;
        std::wstring body = trimmed;
        if (const auto bar = trimmed.find(L'|'); bar != std::wstring::npos) {
            const auto head = trimmed.substr(0, bar);
            const bool numeric = !head.empty() && std::all_of(head.begin(), head.end(), [](wchar_t ch) { return ch >= L'0' && ch <= L'9'; });
            if (numeric) {
                weight = ClampWeight(static_cast<int>(std::wcstol(head.c_str(), nullptr, 10)));
                body = trimmed.substr(bar + 1);
            }
        }
        body = TrimWide(body);
        if (body.empty()) continue;
        lines.push_back(QuoteLine{body, weight});
    }
    return lines;
}

std::wstring FormatQuoteText(const std::vector<QuoteLine>& lines) {
    std::wostringstream out;
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index) out << L"\r\n";
        out << ClampWeight(lines[index].weight) << L'|' << lines[index].text;
    }
    return out.str();
}

std::string QuoteJson(const std::vector<QuoteLine>& lines) {
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < lines.size(); ++index) {
        if (index) out << ',';
        out << "{\"t\":\"" << EscapeJsonBody(lines[index].text) << "\",\"w\":" << ClampWeight(lines[index].weight) << '}';
    }
    out << ']';
    return out.str();
}

bool FindQuoteSpan(const std::string& configText, std::size_t& begin, std::size_t& end) {
    return FindJsonValueSpan(configText, "quotes", '[', ']', begin, end);
}

std::vector<QuoteLine> ParseQuoteJson(const std::string& configText) {
    std::vector<QuoteLine> lines;
    std::size_t begin = 0;
    std::size_t end = 0;
    if (!FindQuoteSpan(configText, begin, end)) return lines;
    std::size_t index = begin + 1;
    while (index < end) {
        const auto open = FindOutsideString(configText, '{', index, end);
        if (open == std::string::npos) break;
        const auto close = FindOutsideString(configText, '}', open + 1, end);
        if (close == std::string::npos) break;
        const auto object = configText.substr(open + 1, close - open - 1);
        index = close + 1;
        const auto keyAt = object.find("\"t\"");
        if (keyAt == std::string::npos) continue;
        const auto valueAt = object.find('"', keyAt + 3);
        std::string value;
        std::size_t valueEnd = 0;
        if (valueAt == std::string::npos || !ReadJsonString(object, valueAt, value, valueEnd)) continue;
        if (value.empty()) continue;
        lines.push_back(QuoteLine{Utf8ToWide(value), ClampWeight(NumberAfter(object, "w", 1))});
    }
    return lines;
}

std::string SetQuoteJson(const std::string& configText, const std::vector<QuoteLine>& lines) {
    return SetJsonValue(configText, "quotes", QuoteJson(lines));
}

std::size_t PickQuote(const std::vector<QuoteLine>& lines, std::size_t avoid, std::mt19937& rng) {
    if (lines.empty()) return 0;
    if (lines.size() == 1) return 0;
    int total = 0;
    for (const auto& line : lines) total += ClampWeight(line.weight);
    std::uniform_real_distribution<double> roll(0.0, static_cast<double>(total));
    std::size_t pick = lines.size() - 1;
    for (int guard = 0; guard < kMaxPickTries; ++guard) {
        const double target = roll(rng);
        double accumulated = 0;
        pick = lines.size() - 1;
        for (std::size_t index = 0; index < lines.size(); ++index) {
            accumulated += ClampWeight(lines[index].weight);
            if (target < accumulated) {
                pick = index;
                break;
            }
        }
        if (pick != avoid) break;
    }
    return pick;
}

} // namespace whale
