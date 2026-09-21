#include "../core/task_end.hpp"

#include <cassert>
#include <iostream>
#include <string>

int main() {
    // The preset list mirrors the upstream sound picker, in its order.
    assert(whale::kTaskEndPresetCount == 4);
    assert(std::string(whale::TaskEndPresetAt(0)) == "preset:duck:press");
    assert(std::string(whale::TaskEndPresetAt(1)) == "preset:duck:release");
    assert(std::string(whale::TaskEndPresetAt(2)) == "preset:fx1:press");
    assert(std::string(whale::TaskEndPresetAt(3)) == "preset:fx1:release");
    assert(std::string(whale::TaskEndPresetAt(-1)) == whale::TaskEndDefaultPreset());
    assert(std::string(whale::TaskEndPresetAt(99)) == whale::TaskEndDefaultPreset());

    // Labels are the upstream option labels.
    assert(whale::TaskEndPresetLabel("preset:duck:press") == L"小黄鸭·按下");
    assert(whale::TaskEndPresetLabel("preset:duck:release") == L"小黄鸭·松开");
    assert(whale::TaskEndPresetLabel("preset:fx1:press") == L"音效1·按下");
    assert(whale::TaskEndPresetLabel("preset:fx1:release") == L"音效1·松开");

    // duck plays the duck pair (set 1: Ya1/Ya2), fx1 the built-in pair (set 0: D1/D2).
    int set = -1;
    bool press = false;
    assert(whale::ParseTaskEndPreset("preset:duck:press", set, press) && set == 1 && press);
    assert(whale::ParseTaskEndPreset("preset:duck:release", set, press) && set == 1 && !press);
    assert(whale::ParseTaskEndPreset("preset:fx1:press", set, press) && set == 0 && press);
    assert(whale::ParseTaskEndPreset("preset:fx1:release", set, press) && set == 0 && !press);

    // Ids this port cannot play, and an empty selection, fall back to the default.
    assert(!whale::ParseTaskEndPreset("grp:duck", set, press));
    assert(!whale::ParseTaskEndPreset("", set, press));
    assert(std::string(whale::TaskEndEffectivePreset("grp:duck")) == whale::TaskEndDefaultPreset());
    assert(std::string(whale::TaskEndEffectivePreset("")) == whale::TaskEndDefaultPreset());
    assert(std::string(whale::TaskEndEffectivePreset("preset:fx1:release")) == "preset:fx1:release");

    // The tray cycle walks the picker order and wraps around.
    assert(whale::NextTaskEndPreset("") == "preset:duck:release");
    assert(whale::NextTaskEndPreset("preset:duck:release") == "preset:fx1:press");
    assert(whale::NextTaskEndPreset("preset:fx1:release") == "preset:duck:press");
    assert(whale::NextTaskEndPreset("grp:duck") == "preset:duck:release");

    // Absent key: upstream defaults are off with an empty selection.
    const auto missing = whale::ParseTaskEndJson("{\"size\":440,\"turnNotice\":1}");
    assert(!missing.on && missing.sel.empty());

    // Upstream stores a real boolean; the flat 0/1 form is accepted too.
    const auto upstreamStyle = whale::ParseTaskEndJson("{\"taskEnd\":{\"on\":true,\"sel\":\"preset:fx1:release\"}}");
    assert(upstreamStyle.on && upstreamStyle.sel == "preset:fx1:release");
    const auto numeric = whale::ParseTaskEndJson("{\"taskEnd\":{\"on\":0,\"sel\":\"\"}}");
    assert(!numeric.on && numeric.sel.empty());

    // JSON round trip.
    const whale::TaskEndSound value{true, "preset:duck:release"};
    const auto json = whale::TaskEndJson(value);
    assert(json == "{\"on\":1,\"sel\":\"preset:duck:release\"}");
    const auto back = whale::ParseTaskEndJson("{\"size\":440,\"taskEnd\":" + json + "}");
    assert(back.on && back.sel == value.sel);

    const auto withQuote = whale::ParseTaskEndJson("{\"taskEnd\":" + whale::TaskEndJson({false, "a\"b\\c"}) + "}");
    assert(!withQuote.on && withQuote.sel == "a\"b\\c");

    // Appending keeps the surrounding keys; replacing drops the old value.
    const auto appended = whale::SetTaskEndJson("{\"size\":440}\n", value);
    assert(appended.find("\"size\":440") != std::string::npos);
    assert(whale::ParseTaskEndJson(appended).sel == value.sel);

    const auto replaced = whale::SetTaskEndJson(
        "{\"size\":440,\"taskEnd\":{\"on\":0,\"sel\":\"preset:fx1:press\"},\"turnNotice\":1}", value);
    const auto replacedValue = whale::ParseTaskEndJson(replaced);
    assert(replacedValue.on && replacedValue.sel == "preset:duck:release");
    assert(replaced.find("preset:fx1:press") == std::string::npos);
    assert(replaced.find("\"turnNotice\":1") != std::string::npos);

    // The span helper leaves the settings window able to copy the object verbatim.
    std::size_t begin = 0;
    std::size_t end = 0;
    assert(whale::FindTaskEndSpan(replaced, begin, end));
    assert(replaced.substr(begin, end - begin) == json);

    std::cout << "native task end tests passed\n";
}
