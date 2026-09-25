// SPDX-License-Identifier: GPL-3.0-or-later
#include "AutoLevel.hpp"

#include <algorithm>
#include <cmath>

namespace titv::dsp {

float AutoLevel::gainFor(float measuredRmsDb, float minGainDb, float maxGainDb) noexcept
{
    return std::clamp(kTargetRmsDb - measuredRmsDb, minGainDb, maxGainDb);
}

void AutoLevel::prepare(double sampleRate) noexcept
{
    windowLength_ = std::max<uint32_t>(1, static_cast<uint32_t>(kWindowSeconds * sampleRate));
    windowFill_ = 0;
    windowPower_ = 0.0;
    windowPeak_ = 0.0f;
    windowBad_ = false;
}

void AutoLevel::closeWindow() noexcept
{
    const double meanPower = windowPower_ / windowFill_;
    const float rmsDb = meanPower > 1e-15 ? static_cast<float>(10.0 * std::log10(meanPower)) : -150.0f;
    const float peakDb = windowPeak_ > 0.0f ? 20.0f * std::log10(windowPeak_) : -150.0f;

    if (!windowBad_ && rmsDb > kVoiceFloorDb && peakDb - rmsDb <= kMaxCrestDb) {
        windowsDb_[windowCount_++] = rmsDb;
        progress_.store(static_cast<float>(windowCount_) / kWindowsNeeded, std::memory_order_relaxed);
        if (windowCount_ == kWindowsNeeded) {
            // Median of the voiced windows: robust to the loudest and softest phrases.
            auto mid = windowsDb_.begin() + kWindowsNeeded / 2;
            std::nth_element(windowsDb_.begin(), mid, windowsDb_.end());
            result_.store(*mid, std::memory_order_relaxed);
            state_.store(State::Done, std::memory_order_release);
        }
    }
    windowFill_ = 0;
    windowPower_ = 0.0;
    windowPeak_ = 0.0f;
    windowBad_ = false;
}

void AutoLevel::process(const float* left, const float* right, uint32_t frames, bool hadNonFinite) noexcept
{
    switch (command_.exchange(kNone, std::memory_order_acq_rel)) {
    case kStart:
        windowCount_ = 0;
        windowFill_ = 0;
        windowPower_ = 0.0;
        windowPeak_ = 0.0f;
        windowBad_ = false;
        progress_.store(0.0f, std::memory_order_relaxed);
        state_.store(State::Listening, std::memory_order_release);
        break;
    case kCancel:
        if (state_.load(std::memory_order_relaxed) == State::Listening)
            state_.store(State::Idle, std::memory_order_release);
        break;
    default:
        break;
    }

    if (state_.load(std::memory_order_relaxed) != State::Listening)
        return;

    windowBad_ |= hadNonFinite;
    for (uint32_t i = 0; i < frames; ++i) {
        const float l = left[i], r = right[i];
        windowPower_ += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        windowPeak_ = std::max(windowPeak_, std::max(std::fabs(l), std::fabs(r)));
        if (++windowFill_ == windowLength_) {
            closeWindow();
            if (state_.load(std::memory_order_relaxed) != State::Listening)
                return;
        }
    }
}

float AutoLevel::takeResult() noexcept
{
    const float r = result_.load(std::memory_order_relaxed);
    state_.store(State::Idle, std::memory_order_release);
    return r;
}

} // namespace titv::dsp
