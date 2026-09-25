// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace titv::dsp {

// Auto Level measurement. The audio thread cuts the raw input into
// 50 ms windows and keeps those that contain voice: loud enough, free of clicks
// (excessive crest factor) and of non-finite samples. After 10 s of such windows it
// publishes their median RMS. The editor then turns it into an Input gain.
//
// start() and cancel() may be called from any thread; process() runs on the audio
// thread; results are read with state(), progress() and takeResult().
class AutoLevel {
public:
    enum class State : int { Idle, Listening, Done };

    static constexpr float kWindowSeconds = 0.050f;
    static constexpr uint32_t kWindowsNeeded = 200; // 10 s of voice
    static constexpr float kVoiceFloorDb = -50.0f;
    static constexpr float kMaxCrestDb = 24.0f;
    static constexpr float kTargetRmsDb = -18.0f;

    // Input gain that brings a measured RMS to the target, within the Input range.
    static float gainFor(float measuredRmsDb, float minGainDb, float maxGainDb) noexcept;

    void prepare(double sampleRate) noexcept;

    void start() noexcept { command_.store(kStart, std::memory_order_release); }
    void cancel() noexcept { command_.store(kCancel, std::memory_order_release); }

    // Audio thread. hadNonFinite marks the block as containing samples that were
    // replaced by silence.
    void process(const float* left, const float* right, uint32_t frames, bool hadNonFinite) noexcept;

    State state() const noexcept { return state_.load(std::memory_order_acquire); }
    float progress() const noexcept { return progress_.load(std::memory_order_relaxed); }

    // Returns the measured median RMS in dBFS and goes back to Idle.
    float takeResult() noexcept;

private:
    static constexpr int kNone = 0, kStart = 1, kCancel = 2;

    void closeWindow() noexcept;

    uint32_t windowLength_ = 2400;
    uint32_t windowFill_ = 0;
    double windowPower_ = 0.0;
    float windowPeak_ = 0.0f;
    bool windowBad_ = false;

    std::array<float, kWindowsNeeded> windowsDb_ {};
    uint32_t windowCount_ = 0;

    std::atomic<int> command_ { kNone };
    std::atomic<State> state_ { State::Idle };
    std::atomic<float> progress_ { 0.0f };
    std::atomic<float> result_ { 0.0f };
};

} // namespace titv::dsp
