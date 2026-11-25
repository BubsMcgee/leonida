// Gives RAM back once the title's own frames replace the splash.
//
// The title's startup leaves cold pages resident: the PE images, the package/splash decode, the warm-up heap of a
// first world load. None of it has to stay resident while the world streams. On Windows a working-set trim asks the
// manager to page out what is not referenced, which the XMem physical-page window can do because its backing is a
// sparse file (runtime/shims/xmem.cpp), and which private heap pages do by going to the pagefile.
//
// It runs once, five seconds after the splash hands off, so the first world frames render before anything is paged
// out. LEONIDA_TRIM_WORKING_SET=0 disables it. No effect on other platforms.
#pragma once

#include <cstddef>

#if defined(_WIN32)
#include <windows.h>

#include <atomic>
#include <chrono>
#include <limits>
#include <thread>
#endif

namespace leonida {

// `text` is LEONIDA_TRIM_WORKING_SET's value: unset or empty is on, a leading '0' is off.
inline bool trim_working_set_enabled(const char* text) { return !text || !*text || text[0] != '0'; }

// Schedules the one-shot trim. Safe to call on every startup-handoff frame: only the first call does anything.
inline void trim_working_set_after_startup() {
#if defined(_WIN32)
    char text[8]{};
    std::size_t length = 0;
    if (getenv_s(&length, text, sizeof(text), "LEONIDA_TRIM_WORKING_SET") == 0 && length > 1 &&
        !trim_working_set_enabled(text))
        return;
    static std::atomic<bool> scheduled{false};
    if (scheduled.exchange(true, std::memory_order_relaxed)) return;
    try {
        std::thread([] {
            std::this_thread::sleep_for(std::chrono::seconds(5));
            SetProcessWorkingSetSize(GetCurrentProcess(), std::numeric_limits<SIZE_T>::max(),
                                     std::numeric_limits<SIZE_T>::max());
        }).detach();
    } catch (...) {
        // A host that cannot start the thread keeps the pages resident; the splash handoff still proceeds.
    }
#endif
}

}  // namespace leonida
