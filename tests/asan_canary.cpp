// Deliberate heap-buffer-overflow, so AddressSanitizer can prove it is armed.
//
// The UBSan half of this pair (sanitizer_canary.cpp) explains the reasoning at
// length: a sanitizer that was silently not applied produces exactly the same
// green suite as a codebase with no defects, and every clean run is
// indistinguishable from a dropped flag until something commits a real error
// on purpose. That argument is not specific to UBSan, but for a while the
// proof was — the nightly ran an ASan leg with no armed-ness check at all, so
// "ASan found nothing" and "ASan was not running" were the same log.
//
// Built and registered only under ENABLE_ASAN, so ordinary builds never see it.
// ASan has no recovering mode here (halt_on_error defaults on for this class),
// so an instrumented run must abort AND print a diagnostic naming the overflow.
//
// A read, not a write: a one-byte out-of-bounds write can corrupt the
// allocator's own bookkeeping, and a canary should not risk taking the
// reporting machinery down with it before the report is produced.

#include <cstdio>
#include <cstdlib>

int main()
{
    // volatile so the optimiser cannot fold the size, prove the index in
    // range, or drop the allocation entirely — any of which would leave
    // nothing for ASan to catch and turn this test into a false alarm.
    volatile int size = 16;
    volatile int index = 20;  // past the end

    char* buffer = static_cast<char*>(std::malloc(static_cast<size_t>(size)));
    if (!buffer) {
        std::fprintf(stderr, "canary: malloc failed, nothing was tested\n");
        return 2;  // distinct from both the trap and the no-trap path
    }
    for (int i = 0; i < size; ++i)
        buffer[i] = static_cast<char>(i);

    // Heap read past the end of the allocation. Under ASan this aborts here
    // with "heap-buffer-overflow" and nothing below runs.
    const char stolen = buffer[index];

    // Only reached when the process was NOT instrumented.
    std::printf("canary: no sanitizer trap (read %d past end, got %d)\n",
                index, static_cast<int>(stolen));
    std::free(buffer);
    return 0;
}
