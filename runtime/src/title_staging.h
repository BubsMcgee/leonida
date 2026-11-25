#pragma once
// Per-texture streaming state for the title textures: which mip tail a read covered, whether a partly mapped read
// must be retried, the shared per-pass upload budget, and a submitting thread's reusable read buffer. Header-only so
// the D3D12 layer and its tests share one definition.
//
// The middle-page case: a streamed title texture whose large mips' pages have not arrived can still upload the
// smaller tail that is mapped. first_uploadable() picks the lowest-index readable mip (the largest resolution in
// that tail); the smaller mips after it are the rest of what it uploads.
//
// This reimplements, in Leonida's own names, behaviour observed in the DexNom/VIctory Radeon runtime's texture
// staging (docs/related_projects.md); no code or identifiers are copied.

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace leonida {

// One texture's upload state.
//   first    lowest-index mip the last complete read covered, with every smaller mip; -1: none.
//   partial  frame of the last read that found only part of the image mapped; ~0: none.
//   used     frame a list last sampled the texture (for ordering/eviction).
struct TitleStaging {
    int first = -1;
    std::uint64_t partial = ~std::uint64_t{0};
    std::uint64_t used = 0;
    bool dirty = true;
    bool uploaded = false;

    bool wants_upload() const { return dirty || !uploaded; }

    // The lowest-index mip whose bytes are readable now, or -1 when none is. The smaller mips after it are the
    // tail the caller uploads with it; a texture whose first pages are still streaming therefore uploads what it
    // has instead of waiting for the whole image.
    template <class Readable>
    static int first_uploadable(int mips, Readable readable) {
        for (int mip = 0; mip < mips; ++mip)
            if (readable(mip)) return mip;
        return -1;
    }

    // Record a read that covered mips [first_mip, mips): complete at >= 0, partial (nothing) at -1.
    void note_read(int first_mip, std::uint64_t frame) {
        first = first_mip;
        uploaded = first_mip >= 0;
        partial = first_mip >= 0 ? ~std::uint64_t{0} : frame;
        dirty = first_mip < 0;
    }

    void note_dirty() {
        dirty = true;
        uploaded = false;
    }

    void note_used(std::uint64_t frame) { used = frame; }
};

// One pass's allowance for texture uploads. New textures and retries draw from the same bytes and items, so
// neither can starve the other.
class TitleUploadBudget {
public:
    TitleUploadBudget(std::uint64_t bytes, unsigned items) : bytes_(bytes), items_(items) {}

    bool take(std::uint64_t bytes) {
        if (!items_ || bytes > bytes_) return false;
        --items_;
        bytes_ -= bytes;
        return true;
    }

    bool empty() const { return items_ == 0; }
    std::uint64_t bytes() const { return bytes_; }
    unsigned items() const { return items_; }

private:
    std::uint64_t bytes_;
    unsigned items_;
};

// A submitting thread's reusable read buffer. Contents carry over between uses; the caller fills them.
class TitleReadScratch {
public:
    template <class F>
    auto with(std::size_t size, F&& f) -> decltype(f(std::declval<std::uint8_t*>())) {
        if (bytes_.size() < size) bytes_.resize(size);
        return f(bytes_.data());
    }

    std::size_t capacity() const { return bytes_.size(); }

private:
    std::vector<std::uint8_t> bytes_;
};

}  // namespace leonida
