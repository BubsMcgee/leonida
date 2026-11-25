// The registry of title-window ranges a shader wrote: how marks are split over the windows that hold them, merged
// into one run, and taken once for the copy-back (src/title_write_back.h). Synthetic memory only: no GPU and no
// game data.
#include "../src/title_write_back.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace wb = leonida::title_write_back;

namespace {

int g_failures = 0;

void check(bool ok, const char* what) {
    std::printf("%s %s\n", ok ? "PASS" : "FAIL", what);
    if (!ok) ++g_failures;
}

}  // namespace

int main() {
    std::vector<std::uint8_t> first(64, 0x11), second(64, 0x22);
    wb::Registry registry;
    registry.add_window(0x10000, first.data(), first.size());
    registry.add_window(0x20000, second.data(), second.size());

    check(registry.empty(), "a fresh registry has nothing to copy back");
    check(registry.take().empty(), "taking from a fresh registry gives nothing");

    // A mark outside every window is ignored (a slot-133 copy is not a window).
    registry.mark(0x30000, 32);
    check(registry.empty(), "a mark outside the windows is not kept");

    // A mark inside the first window carries the copy's own address and offset.
    registry.mark(0x10010, 0x20);
    check(!registry.empty(), "a mark inside a window is kept");
    auto ranges = registry.take();
    check(ranges.size() == 1 && ranges[0].title_va == 0x10010 && ranges[0].shadow == first.data() + 0x10 &&
              ranges[0].bytes == 0x20,
          "the mark is pinned to the first window's copy");

    // Adjacent marks merge into one run; a mark that reaches into the second window is split there.
    registry.mark(0x10030, 0x20);
    registry.mark(0x10010, 0x20);
    registry.mark(0x20008, 0x8);
    registry.mark(0x10000, 0x10);
    ranges = registry.take();
    bool merged_first = false, split_second = false;
    for (const auto& r : ranges) {
        if (r.title_va == 0x10000 && r.shadow == first.data() && r.bytes == 0x40) merged_first = true;
        if (r.title_va == 0x20008 && r.shadow == second.data() + 0x8 && r.bytes == 0x8) split_second = true;
    }
    check(merged_first, "adjacent marks in one window are one run");
    check(split_second, "a mark in the second window keeps that window's copy");
    check(registry.empty(), "taking clears what was marked");

    // The window's end is respected: a mark starting past it, or running past it, yields only the overlap.
    registry.mark(0x1003f, 0x4);
    ranges = registry.take();
    check(ranges.size() == 1 && ranges[0].bytes == 1, "a mark is clipped at the window's end");

    if (g_failures) {
        std::printf("%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("title_write_back: OK\n");
    return 0;
}
