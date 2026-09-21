#pragma once

namespace whale {

// Signalled by the overlay on a full exit so the supervisor stops waiting for
// the next Codex start. "本次退出挂件" only closes the window and leaves the
// event untouched.
inline constexpr wchar_t kSupervisorStopEvent[] = L"Local\\ApiBalanceWhaleSupervisorStop";

} // namespace whale
