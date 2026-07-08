#pragma once

namespace poly {

enum class LogLevel { Debug, Info, Warning, Error };

// -----------------------------------------------------------------------
// Minimal file-backed logger. Every call is independently safe even if
// init() was never called (writes are simply dropped), so subsystems
// can log diagnostics from very early in startup without ordering
// concerns.
// -----------------------------------------------------------------------
class Logger {
public:
    static void init();
    static void shutdown();

    static void log(LogLevel level, const char* fmt, ...);

    static constexpr const char* kLogPath = "sdmc:/3ds/hexonquest/log.txt";
};

// Logs the formatted message at Error level, then blocks on the 3DS
// system error applet to show it directly on the console (useful for
// startup failures where there is no in-game HUD available yet to
// report the problem another way).
void reportFatalError(const char* fmt, ...);

} // namespace poly
