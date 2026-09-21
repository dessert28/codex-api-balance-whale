#pragma once

#include <filesystem>
#include <string>

namespace whale {

// Per-user scheduled task registered by install.ps1.
extern const wchar_t* const kScheduledTaskName;
// Per-user Run key value written by the tray / settings toggle.
extern const wchar_t* const kRunValueName;

std::filesystem::path CurrentExecutablePath();

bool IsAutoStartEnabled();
// enable=true writes the per-user Run key and removes the scheduled task so
// both mechanisms never fight over a single supervisor. enable=false removes
// both.
bool SetAutoStart(bool enable, std::wstring* error = nullptr);

} // namespace whale
