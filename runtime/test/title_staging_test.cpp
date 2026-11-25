// TitleStaging / TitleUploadBudget / TitleReadScratch (runtime/src/title_staging.h): the mip-tail plan for a partly
// mapped streamed texture, the shared upload budget, and the reusable read scratch. CPU only, no device.

#include "../src/title_staging.h"

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;
void check(bool ok, const char* what) {
    if (ok) return;
    std::printf("FAIL: %s\n", what);
    ++failures;
}

}  // namespace

int main() {
    using leonida::TitleStaging;
    using leonida::TitleUploadBudget;
    using leonida::TitleReadScratch;

    // Whole image readable: the tail starts at mip 0.
    check(TitleStaging::first_uploadable(4, [](int) { return true; }) == 0, "all readable -> mip 0");
    // Only the middle/small mips have arrived; the largest are still streaming.
    const bool readable[4] = {false, false, true, true};
    check(TitleStaging::first_uploadable(4, [&](int mip) { return readable[mip]; }) == 2,
          "middle pages -> lowest readable mip");
    check(TitleStaging::first_uploadable(4, [](int) { return false; }) == -1, "nothing readable -> -1");

    TitleStaging s;
    check(s.wants_upload(), "a new texture wants an upload");
    s.note_read(2, 10);
    check(s.first == 2 && s.uploaded && !s.wants_upload() && s.partial == ~std::uint64_t{0},
          "a complete tail read clears the request");
    s.note_dirty();
    check(s.wants_upload() && !s.uploaded, "a write asks for another upload");
    s.note_read(-1, 12);
    check(s.wants_upload() && s.partial == 12, "a partial read keeps it for the next pass");

    TitleUploadBudget b(100, 2);
    check(b.take(60) && b.bytes() == 40 && b.items() == 1, "first upload admitted");
    check(!b.take(60), "a retry cannot overrun the shared budget");
    check(b.take(40) && b.empty() && b.bytes() == 0, "new upload and retry share the budget");

    TitleReadScratch read;
    const auto n = read.with(16, [](std::uint8_t* p) {
        p[0] = 7;
        return std::uint8_t{p[0]};
    });
    check(n == 7 && read.capacity() >= 16, "the scratch hands out a buffer");
    read.with(4, [](std::uint8_t*) {});
    check(read.capacity() >= 16, "a smaller use keeps the larger buffer");

    if (failures) {
        std::printf("%d failure(s)\n", failures);
        return 1;
    }
    std::puts("title_staging: ok");
    return 0;
}
