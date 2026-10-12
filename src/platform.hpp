#pragma once
#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <psapi.h>
#else
#include <sys/resource.h>
#endif

namespace cap_platform {
inline bool replace_file(const std::string& source, const std::string& target) {
#ifdef _WIN32
    return MoveFileExA(source.c_str(), target.c_str(),
                      MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
#else
    return std::rename(source.c_str(), target.c_str()) == 0;
#endif
}
inline std::uint64_t peak_working_set_bytes() {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS memory{};
    memory.cb = sizeof(memory);
    if (!GetProcessMemoryInfo(GetCurrentProcess(), &memory, sizeof(memory)))
        throw std::runtime_error("Cannot read peak working set");
    return memory.PeakWorkingSetSize;
#else
    struct rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage))
        throw std::runtime_error("Cannot read peak resident set size");
#ifdef __APPLE__
    return static_cast<std::uint64_t>(usage.ru_maxrss);
#else
    return static_cast<std::uint64_t>(usage.ru_maxrss) * 1024;
#endif
#endif
}
}
