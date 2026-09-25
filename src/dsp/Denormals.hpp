// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#if defined(__SSE__) || defined(_M_X64) || defined(_M_IX86)
  #include <xmmintrin.h>
  #define TITV_HAS_SSE_CSR 1
#endif

namespace titv::dsp {

// Enables flush-to-zero / denormals-are-zero for the current thread and restores
// the previous mode on destruction. Construct one at the top of each audio callback.
class ScopedNoDenormals {
public:
    ScopedNoDenormals() noexcept
    {
#if defined(TITV_HAS_SSE_CSR)
        const unsigned csr = _mm_getcsr();
        saved_ = csr;
        _mm_setcsr(csr | 0x8040u); // FTZ (bit 15) | DAZ (bit 6)
#elif defined(__aarch64__)
        uint64_t fpcr;
        asm volatile("mrs %0, fpcr" : "=r"(fpcr));
        saved_ = fpcr;
        asm volatile("msr fpcr, %0" : : "r"(fpcr | (1ull << 24))); // FZ
#endif
    }

    ~ScopedNoDenormals() noexcept
    {
#if defined(TITV_HAS_SSE_CSR)
        _mm_setcsr(static_cast<unsigned>(saved_));
#elif defined(__aarch64__)
        asm volatile("msr fpcr, %0" : : "r"(saved_));
#endif
    }

    ScopedNoDenormals(const ScopedNoDenormals&) = delete;
    ScopedNoDenormals& operator=(const ScopedNoDenormals&) = delete;

private:
    [[maybe_unused]] uint64_t saved_ = 0;
};

} // namespace titv::dsp
