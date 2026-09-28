// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>

namespace titv::dsp {

// Voice register: the pitch and sibilance range the processing is tuned for.
// Neutral is the original tuning, so it must keep the historical values exactly.
enum class VoiceRegister : uint8_t { Male, Neutral, Female };

struct RegisterTuning {
    double inputHpfHz, outputHpfHz;          // HPF
    double bodyHz, midHz, presenceHz;        // TONE bands (Air stays put)
    double colorDipHz, colorPresenceHz;      // TONE fixed colour curve
    double deEssHz, deEssDetectorHighPassHz; // De-Ess
    double saturatePreHighPassHz;            // Saturate
    double radioLowHz;                       // Radio
};

// Values measured on synthetic voices, tuned to match several speech studies
// (voice pitch, formants, average spectrum, "s" and "ch" sounds):
// - voice pitch, men ~120 Hz, women ~210 Hz (HPF):
//   https://www.researchgate.net/publication/240312210_The_frequency_range_of_the_voice_fundamental_in_the_speech_of_male_and_female_adults
//   https://en.wikipedia.org/wiki/Voice_frequency
// - formants, women 15-20 % higher (Body, Mid, Presence, colour curve):
//   https://pmc.ncbi.nlm.nih.gov/articles/PMC6002811/ (Hillenbrand 1995)
//   https://rdrr.io/cran/phonTools/man/pb52.html (Peterson & Barney 1952)
// - average spectrum men vs women (315 Hz, 800-1000 Hz, 6-12 kHz):
//   https://pubs.aip.org/asa/jasa/article/156/5/3056/3319043/Gender-and-speech-material-effects-on-the-long
//   https://pubs.aip.org/asa/jasa/article/96/4/2108/832716/An-international-comparison-of-long-term-average
// - "s" and "ch" spectrum, higher for women (De-Ess):
//   https://kuppl.ku.edu/sites/kuppl/files/documents/publications/Jongman%20OREL%202024%20Phonetics%20of%20Fricatives.pdf
//   https://pmc.ncbi.nlm.nih.gov/articles/PMC10651311/
//   https://www.researchgate.net/publication/12314438_Acoustic_characteristics_of_English_fricatives
// TODO: careful the day we add a parametric EQ, the user will change the band
// frequencies. Then the register should maybe scale the user frequencies
// instead of replacing them.
inline constexpr std::array<RegisterTuning, 3> kRegisterTunings { {
    { 65.0, 72.0, 150.0, 1100.0, 4000.0, 250.0, 2600.0, 5500.0, 3500.0, 120.0, 300.0 },  // Male
    { 90.0, 100.0, 180.0, 1200.0, 4500.0, 280.0, 2800.0, 7000.0, 4500.0, 150.0, 350.0 }, // Neutral
    { 120.0, 133.0, 220.0, 1300.0, 5000.0, 320.0, 3000.0, 8500.0, 4500.0, 180.0, 400.0 }, // Female
} };

constexpr const RegisterTuning& tuning(VoiceRegister r) noexcept { return kRegisterTunings[static_cast<size_t>(r)]; }

} // namespace titv::dsp
