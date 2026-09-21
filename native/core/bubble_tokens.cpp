#include "bubble_tokens.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <sstream>

#include <windows.h>

namespace whale {
namespace {

constexpr wchar_t kMissing[] = L"--";

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) return {};
    const int length = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (length <= 0) return {};
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), result.data(), length);
    return result;
}

std::wstring Missing() {
    return kMissing;
}

std::wstring Percent(const std::optional<UsageWindow>& window) {
    if (!window) return Missing();
    return std::to_wstring(static_cast<long long>(std::lround(window->usedPercent))) + L"%";
}

// Same shape as the quota card countdown, minus the trailing 后重置: a line like
// 「5 小时额度 2小时13分钟」reads better than「2小时13分钟后重置」.
std::wstring Remaining(std::int64_t resetsAt, std::chrono::system_clock::time_point now) {
    if (resetsAt <= 0) return Missing();
    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();
    const auto left = resetsAt - nowSeconds;
    if (left <= 0) return Missing();
    const auto minutes = (left + 59) / 60;
    const auto days = minutes / 1440;
    const auto hours = (minutes % 1440) / 60;
    const auto rest = minutes % 60;
    std::wostringstream text;
    if (days) {
        text << days << L"天";
        if (hours) text << hours << L"小时";
    } else if (hours) {
        text << hours << L"小时";
        if (rest) text << rest << L"分钟";
    } else {
        text << rest << L"分钟";
    }
    return text.str();
}

std::wstring Clock(std::chrono::system_clock::time_point now, bool dateOnly) {
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
    if (localtime_s(&local, &seconds) != 0) return Missing();
    wchar_t buffer[32]{};
    if (dateOnly) {
        swprintf(buffer, 32, L"%04d-%02d-%02d", local.tm_year + 1900, local.tm_mon + 1, local.tm_mday);
    } else {
        swprintf(buffer, 32, L"%02d:%02d", local.tm_hour, local.tm_min);
    }
    return buffer;
}

} // namespace

const std::vector<std::wstring>& BubbleTokenNames() {
    static const std::vector<std::wstring> names{
        L"p5h", L"week", L"reset5h", L"resetweek", L"turn", L"today", L"tokens7d", L"model", L"date", L"time"};
    return names;
}

std::wstring BubbleTokenListText() {
    std::wstring text;
    for (const auto& name : BubbleTokenNames()) {
        if (!text.empty()) text += L' ';
        text += L'{' + name + L'}';
    }
    return text;
}

std::vector<BubbleToken> BubbleTokenValues(const UsageSnapshot& snapshot, std::chrono::system_clock::time_point now) {
    std::wstring turn = Missing();
    std::wstring model;
    if (snapshot.lastTurn) {
        turn = std::to_wstring(snapshot.lastTurn->deltaTokens);
        model = Utf8ToWide(snapshot.lastTurn->model);
    }
    if (model.empty()) model = Utf8ToWide(snapshot.currentModel);
    if (model.empty()) model = Missing();

    std::vector<BubbleToken> tokens;
    tokens.push_back({L"p5h", Percent(snapshot.fiveHour)});
    tokens.push_back({L"week", Percent(snapshot.weekly)});
    tokens.push_back({L"reset5h", snapshot.fiveHour ? Remaining(snapshot.fiveHour->resetsAt, now) : Missing()});
    tokens.push_back({L"resetweek", snapshot.weekly ? Remaining(snapshot.weekly->resetsAt, now) : Missing()});
    tokens.push_back({L"turn", turn});
    tokens.push_back({L"today", std::to_wstring(snapshot.todayTokens)});
    tokens.push_back({L"tokens7d", std::to_wstring(snapshot.last7dTokens)});
    tokens.push_back({L"model", model});
    tokens.push_back({L"date", Clock(now, true)});
    tokens.push_back({L"time", Clock(now, false)});
    return tokens;
}

std::wstring ExpandBubbleTokens(const std::wstring& text, const std::vector<BubbleToken>& tokens) {
    if (text.empty()) return text;
    std::vector<const BubbleToken*> ordered;
    ordered.reserve(tokens.size());
    for (const auto& token : tokens) {
        if (!token.name.empty()) ordered.push_back(&token);
    }
    std::stable_sort(ordered.begin(), ordered.end(), [](const BubbleToken* left, const BubbleToken* right) {
        return left->name.size() > right->name.size();
    });
    std::wstring result = text;
    for (const auto* token : ordered) {
        const std::wstring needle = L'{' + token->name + L'}';
        std::size_t at = 0;
        while ((at = result.find(needle, at)) != std::wstring::npos) {
            result.replace(at, needle.size(), token->value);
            at += token->value.size();
        }
    }
    return result;
}

} // namespace whale
