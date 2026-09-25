// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Plugin identity. BRAND_ID, UNIQUE_ID, CLAP_ID and URI identify the plugin in saved
// projects: they must not change once a version has been published.
#define DISTRHO_PLUGIN_BRAND    "pehadavid"
#define DISTRHO_PLUGIN_NAME     "This Is The Voice"
#define DISTRHO_PLUGIN_URI      "https://github.com/pehadavid/ThisIsTheVoice"
#define DISTRHO_PLUGIN_CLAP_ID  "io.github.pehadavid.thisisthevoice"

#define DISTRHO_PLUGIN_BRAND_ID  Phdv
#define DISTRHO_PLUGIN_UNIQUE_ID TiTV

#define DISTRHO_PLUGIN_HAS_UI              1
#define DISTRHO_PLUGIN_IS_RT_SAFE          1
#define DISTRHO_PLUGIN_NUM_INPUTS          2
#define DISTRHO_PLUGIN_NUM_OUTPUTS         2
// Extra channel layouts, used by the AU format only (Logic Pro): mono in / stereo out,
// the usual case of a mono vocal track, and mono in / mono out. VoicePlugin::ioChanged
// passes the actual layout to the engine.
#define DISTRHO_PLUGIN_EXTRA_IO            { 1, 2 }, { 1, 1 }
#define DISTRHO_PLUGIN_WANT_TIMEPOS        1
#define DISTRHO_PLUGIN_WANT_STATE          1
#define DISTRHO_PLUGIN_WANT_FULL_STATE     1
// Tail length reported to VST3 hosts (needs patches/dpf-tail.patch). CLAP hosts keep
// processing the plugin anyway: DPF always asks them to continue.
#define DISTRHO_PLUGIN_WANT_TAIL           1
// Engine::setParameter only stores atomics: DPF may call setParameterValue from the
// VST3 controller thread (needs patches/dpf-vst3-parameter-sync.patch).
#define DISTRHO_PLUGIN_PARAMETERS_THREAD_SAFE 1
// The editor reads the meters straight from the engine instead of through output
// parameters, which would take host parameter IDs.
#define DISTRHO_PLUGIN_WANT_DIRECT_ACCESS  1

#define DISTRHO_PLUGIN_LV2_CATEGORY     "lv2:DynamicsPlugin"
#define DISTRHO_PLUGIN_VST3_CATEGORIES  "Fx|Dynamics|Stereo"
#define DISTRHO_PLUGIN_CLAP_FEATURES    "audio-effect", "compressor", "equalizer", "reverb", "delay", "stereo"

#define DISTRHO_UI_USE_NANOVG      1
#define DISTRHO_UI_USER_RESIZABLE  1
#define DISTRHO_UI_DEFAULT_WIDTH   1130
#define DISTRHO_UI_DEFAULT_HEIGHT  460
