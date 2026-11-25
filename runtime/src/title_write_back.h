#pragma once
// GPU writes into title memory reached through a title window: the windows a shader may write through, and the host
// copy each one lives in, so the title's own pages can be brought up to date once the GPU has finished. Window copies
// live for the device's lifetime, so a marked range never names a freed copy. Adapted from DexNom/VIctory's
// copy-back work (commits cd94d35 and 940d32f, GPL-3.0-or-later), reworked for this layer: a range is marked when a
// buffer view is written and copied back after the shadow refresh, instead of having shaders mark pages.
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

namespace leonida::title_write_back {

struct Window {
    std::uintptr_t title_va = 0;     // first byte of the title's range
    std::uint8_t* shadow = nullptr;  // the host-visible copy of that range
    std::size_t bytes = 0;
};

struct Range {
    std::uintptr_t title_va = 0;
    std::uint8_t* shadow = nullptr;
    std::size_t bytes = 0;
};

class Registry {
public:
    void add_window(std::uintptr_t title_va, std::uint8_t* shadow, std::size_t bytes) {
        if (!shadow || !bytes) return;
        const std::lock_guard lock(mutex_);
        windows_.push_back({title_va, shadow, bytes});
    }

    // Marks every window slice that [title_va, title_va + bytes) overlaps as written by the GPU.
    void mark(std::uintptr_t title_va, std::size_t bytes) {
        if (!bytes || title_va > UINTPTR_MAX - bytes) return;
        const std::uintptr_t end = title_va + bytes;
        const std::lock_guard lock(mutex_);
        for (const Window& w : windows_) {
            const std::uintptr_t from = title_va > w.title_va ? title_va : w.title_va;
            const std::uintptr_t to = end < w.title_va + w.bytes ? end : w.title_va + w.bytes;
            if (from >= to) continue;
            merge(from, w.shadow + (from - w.title_va), to - from);
        }
    }

    // Every marked range, in the order they were first reached, and forgets them.
    std::vector<Range> take() {
        std::vector<Range> out;
        const std::lock_guard lock(mutex_);
        out.swap(marked_);
        return out;
    }

    bool empty() const {
        const std::lock_guard lock(mutex_);
        return marked_.empty();
    }

private:
    void merge(std::uintptr_t va, std::uint8_t* shadow, std::size_t bytes) {
        for (Range& r : marked_) {
            if (r.title_va + r.bytes == va && r.shadow + r.bytes == shadow) {
                r.bytes += bytes;
                return;
            }
            if (va + bytes == r.title_va && shadow + bytes == r.shadow) {
                r.title_va = va;
                r.shadow = shadow;
                r.bytes += bytes;
                return;
            }
        }
        marked_.push_back({va, shadow, bytes});
    }

    mutable std::mutex mutex_;
    std::vector<Window> windows_;
    std::vector<Range> marked_;
};

}  // namespace leonida::title_write_back
