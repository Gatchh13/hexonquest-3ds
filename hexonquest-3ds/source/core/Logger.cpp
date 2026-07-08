#include "core/Logger.h"
#include <3ds.h>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <sys/stat.h>

namespace poly {

namespace {
bool g_initialized = false;

const char* levelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "DEBUG";
        case LogLevel::Info:    return "INFO ";
        case LogLevel::Warning: return "WARN ";
        case LogLevel::Error:   return "ERROR";
        default:                return "?????";
    }
}
} // namespace

void Logger::init() {
    mkdir("sdmc:/3ds", 0777);
    mkdir("sdmc:/3ds/hexonquest", 0777);
    FILE* f = std::fopen(kLogPath, "w");
    if (f) {
        std::fputs("=== Hexonquest log start ===\n", f);
        std::fclose(f);
    }
    g_initialized = true;
}

void Logger::shutdown() {
    if (!g_initialized) return;
    FILE* f = std::fopen(kLogPath, "a");
    if (f) {
        std::fputs("=== Hexonquest log end ===\n", f);
        std::fclose(f);
    }
    g_initialized = false;
}

void Logger::log(LogLevel level, const char* fmt, ...) {
    char message[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    FILE* f = std::fopen(Logger::kLogPath, "a");
    if (f) {
        std::fprintf(f, "[%s] %s\n", levelName(level), message);
        std::fclose(f);
    }
}

void reportFatalError(const char* fmt, ...) {
    char message[256];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);

    Logger::log(LogLevel::Error, "FATAL: %s", message);

    ErrorConf conf;
    errorInit(&conf, ERROR_TEXT_WORD_WRAP, CFG_LANGUAGE_EN);
    errorText(&conf, message);
    errorDisp(&conf);
}

} // namespace poly
