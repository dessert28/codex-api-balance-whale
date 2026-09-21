#pragma once

#include <cstddef>
#include <string>

namespace whale {

// Upstream keeps the turn-end sound in `usageSet.taskEnd = {on, sel}`, where
// `sel` is normally `preset:<set>:<press|release>` (it can also point at an
// audio group or fragment, which this port has no assets for). The same shape
// lives in overlay.json as `"taskEnd":{"on":1,"sel":"preset:duck:press"}`.
struct TaskEndSound {
    bool on{false};
    std::string sel{};
};

// Preset ids this port can actually play, in the order upstream lists them.
constexpr int kTaskEndPresetCount = 4;
const char* TaskEndPresetAt(int index);

// Upstream's sound picker falls back to this preset whenever the stored value is
// missing or unknown.
const char* TaskEndDefaultPreset();

// Effective preset for a stored `sel`: one of ours, or the default fallback.
const char* TaskEndEffectivePreset(const std::string& sel);

// `preset:<set>:<press|release>` -> AudioPlayer set index + press/release.
// `duck` is the duck pair (Ya1/Ya2), `fx1` the built-in pair (D1/D2).
bool ParseTaskEndPreset(const std::string& sel, int& set, bool& press);

// Human label used by the tray item and the settings window.
std::wstring TaskEndPresetLabel(const std::string& sel);

// Next preset in the picker order, wrapping around: drives the tray cycle.
std::string NextTaskEndPreset(const std::string& sel);

// JSON plumbing for the shared config file.
bool FindTaskEndSpan(const std::string& configText, std::size_t& begin, std::size_t& end);
TaskEndSound ParseTaskEndJson(const std::string& configText);
std::string TaskEndJson(const TaskEndSound& value);
std::string SetTaskEndJson(const std::string& configText, const TaskEndSound& value);

} // namespace whale
