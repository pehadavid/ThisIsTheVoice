// SPDX-License-Identifier: GPL-3.0-or-later
// Editor: five sections between the input and output meters, drawn with NanoVG.
// Everything is laid out in base units (kWidth x kHeight). The window accepts any
// size above a minimum; the layout is scaled to fit it, keeping its proportions, and
// centred. The aspect ratio is not imposed on the host: some hosts (Bitwig Studio on
// Linux) then never resize the editor at all.

#include "DistrhoUI.hpp"
#include "PresetBrowser.hpp"
#include "Settings.hpp"
#include "plugin/VoicePlugin.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

START_NAMESPACE_DISTRHO

namespace {

using titv::Param;
using titv::ui::Language;
using titv::ui::Text;

constexpr uint kWidth = DISTRHO_UI_DEFAULT_WIDTH;
constexpr uint kHeight = DISTRHO_UI_DEFAULT_HEIGHT;
// Smallest window, as a share of the base size: still readable on a compact screen.
constexpr double kMinimumScale = 0.75;

constexpr float kMeterFloorDb = -60.0f;
constexpr float kTargetRmsDb = -18.0f; // Auto Level working target
constexpr float kTargetZoneDb = 3.0f;
constexpr uint kDoubleClickMs = 350;
constexpr float kDragPixelsFullRange = 220.0f;
constexpr float kFineDragFactor = 0.1f;

// Knob sweep, in NanoVG angles (clockwise from +x, y pointing down).
constexpr float kKnobStart = 0.75f * static_cast<float>(titv::dsp::kPi);
constexpr float kKnobSweep = 1.5f * static_cast<float>(titv::dsp::kPi);

struct Palette {
    Color background { 0x12, 0x14, 0x17 };
    Color panel { 0x1b, 0x1e, 0x23 };
    Color panelEdge { 0x2a, 0x2e, 0x35 };
    Color track { 0x33, 0x38, 0x40 };
    Color text { 0xe6, 0xe8, 0xeb };
    Color textDim { 0x86, 0x8d, 0x97 };
    Color accent { 0xff, 0xb0, 0x20 };
    Color clip { 0xff, 0x45, 0x3a };
};
const Palette kColors;

enum class Kind { Knob, Toggle, Choice, Led };

struct Control {
    Param param;
    Kind kind;
    float x, y, w, h;     // hit box; knobs are centred in it
    Param section;        // enable toggle of the owning section, or GlobalBypass for none
};

struct Section {
    const char* title;
    float x, w;
    Param enable; // GlobalBypass when the section has no enable toggle
};

constexpr float kSectionTop = 60.0f;
constexpr float kSectionHeight = 344.0f;
constexpr float kHelpTop = 412.0f;

// EN | FR switch in the header, right of the title.
constexpr float kLangX = 226.0f, kLangY = 13.0f, kLangW = 64.0f, kLangH = 24.0f;

// Auto Level buttons, between the input meter and the Input knob.
constexpr float kAutoX = 26.0f, kAutoY = kSectionTop + 238.0f, kAutoW = 56.0f, kAutoH = 18.0f;
constexpr float kUndoX = 86.0f, kUndoW = 30.0f;

// Gain-reduction bars: full length is 20 dB of reduction.
constexpr float kReductionRangeDb = 20.0f;
// Decay of the displayed reduction, in dB per editor refresh.
constexpr float kReductionFallDb = 0.6f;

// Preset selector in the header: previous / name (opens the list) / next.
constexpr float kPresetX = 320.0f, kPresetY = 13.0f, kPresetW = 300.0f, kPresetH = 24.0f;
constexpr float kPresetArrowW = 26.0f;
constexpr float kPresetItemH = 22.0f;
constexpr float kPresetListTop = kPresetY + kPresetH + 4;
constexpr int kPresetListMaxRows = 17;
constexpr float kDeleteZoneW = 70.0f; // right end of a user preset row
constexpr float kEditButtonW = 28.0f;

enum class Button { None, Language, Auto, Undo, PresetPrevious, PresetNext, PresetName, EditConfirm, EditCancel };

// One line of the preset list.
struct PresetRow {
    enum Kind { Entry, UserHeader, SaveAction } kind;
    size_t entry = 0; // index in PresetBrowser::entries() for Entry rows
};

bool inside(float x, float y, float rx, float ry, float rw, float rh)
{
    return x >= rx && x < rx + rw && y >= ry && y < ry + rh;
}

float normalized(Param p, float value)
{
    const titv::ParamInfo& pi = titv::info(p);
    return (value - pi.min) / (pi.max - pi.min);
}

float fromNormalized(Param p, float n)
{
    const titv::ParamInfo& pi = titv::info(p);
    return titv::Engine::clampToRange(p, pi.min + std::clamp(n, 0.0f, 1.0f) * (pi.max - pi.min));
}

void formatValue(Param p, float value, char* buf, size_t size)
{
    const titv::ParamInfo& pi = titv::info(p);
    switch (pi.kind) {
    case titv::ParamKind::Toggle:
        std::snprintf(buf, size, "%s", value >= 0.5f ? "on" : "off");
        return;
    case titv::ParamKind::Choice:
        std::snprintf(buf, size, "%s", titv::kNoteDivisions[static_cast<uint32_t>(value)].label);
        return;
    case titv::ParamKind::Continuous:
        if (std::strcmp(pi.unit, "dB") == 0)
            std::snprintf(buf, size, "%+.1f dB", value);
        else
            std::snprintf(buf, size, "%.0f %s", value, pi.unit);
        return;
    }
}

} // namespace

class VoiceUI : public UI {
public:
    VoiceUI()
        : UI(kWidth, kHeight)
    {
        // Desktop / host scale factor (HiDPI): start at the matching pixel size.
        const double scale = getScaleFactor();
        setGeometryConstraints(static_cast<uint>(kWidth * kMinimumScale * scale),
                               static_cast<uint>(kHeight * kMinimumScale * scale), false, false);
        if (d_isNotEqual(scale, 1.0))
            setSize(static_cast<uint>(kWidth * scale), static_cast<uint>(kHeight * scale));
        loadSharedResources();
        for (const titv::ParamInfo& p : titv::kParams)
            values_[titv::index(p.id)] = p.def;
        language_ = titv::ui::loadLanguage();
        if (auto* plugin = static_cast<VoicePlugin*>(getPluginInstancePointer()))
            engine_ = &plugin->engine();
        buildLayout();
    }

protected:
    void stateChanged(const char* key, const char* value) override
    {
        if (std::strcmp(key, "preset") == 0) {
            presets_.restore(value);
            repaint();
        }
    }

    void parameterChanged(uint32_t index, float value) override
    {
        if (index < titv::kParamCount) {
            values_[index] = value;
            repaint();
        }
    }

    // Meters refresh at the host's idle rate (typically 30-60 Hz).
    void uiIdle() override
    {
        if (engine_ == nullptr)
            return;

        // Auto Level finished: write the result into Input as a host gesture.
        titv::dsp::AutoLevel& autoLevel = engine_->autoLevel();
        if (autoLevel.state() == titv::dsp::AutoLevel::State::Done) {
            const titv::ParamInfo& input = info(Param::InputGainDb);
            const float gain = titv::dsp::AutoLevel::gainFor(autoLevel.takeResult(), input.min, input.max);
            undoGain_ = value(Param::InputGainDb);
            hasUndo_ = true;
            engine_->requestSlowInputRamp();
            commit(Param::InputGainDb, gain);
        }

        compressorReduction_ = std::max(engine_->compressorReductionDb(), compressorReduction_ - kReductionFallDb);
        duckActivity_ = engine_->duckActivity();
        deEsserReduction_ = std::max(engine_->deEsserReductionDb(), deEsserReduction_ - kReductionFallDb);
        repaint();
    }

    void onNanoDisplay() override
    {
        fillColor(kColors.background);
        beginPath();
        rect(0, 0, getWidth(), getHeight());
        fill();

        const View v = view();
        save();
        translate(v.x, v.y);
        scale(v.scale, v.scale);
        drawHeader();
        for (const Section& s : sections_)
            drawSection(s);
        drawMeter(sections_.front(), engine_ != nullptr ? &engine_->inputMeter() : nullptr, true);
        drawMeter(sections_.back(), engine_ != nullptr ? &engine_->outputMeter() : nullptr, false);
        for (const Control& c : controls_)
            drawControl(c);
        drawAutoLevel();
        drawDucking();
        drawPresetSelector();
        drawHelp();
        drawPresetList(); // on top of everything
        restore();
    }

    bool onMouse(const MouseEvent& ev) override
    {
        if (ev.button != 1)
            return false;

        if (!ev.press) {
            if (dragging_ != nullptr)
                editParameter(titv::index(dragging_->param), false);
            dragging_ = nullptr;
            return true;
        }

        const View v = view();
        const float x = v.toBaseX(ev.pos.getX()), y = v.toBaseY(ev.pos.getY());

        // An open preset list takes the click, wherever it lands.
        if (presetListOpen_) {
            onPresetListClick(x, y);
            repaint();
            return true;
        }
        // Naming a preset: the confirm / cancel buttons, anywhere else cancels.
        if (editingName_) {
            const Button b = buttonAt(x, y);
            if (b == Button::EditConfirm)
                confirmSave();
            else if (b != Button::PresetName)
                editingName_ = false;
            repaint();
            return true;
        }

        if (engine_ != nullptr) {
            if (hitMeter(sections_.front(), x, y)) { engine_->inputMeter().clearClip(); return true; }
            if (hitMeter(sections_.back(), x, y)) { engine_->outputMeter().clearClip(); return true; }
        }

        switch (buttonAt(x, y)) {
        case Button::PresetPrevious:
        case Button::PresetNext:
            presets_.refresh();
            applyEntry(presets_.neighbour(buttonAt(x, y) == Button::PresetNext ? 1 : -1));
            return true;
        case Button::PresetName:
            openPresetList();
            return true;
        case Button::EditConfirm:
        case Button::EditCancel:
            return true;
        case Button::Language:
            language_ = language_ == Language::English ? Language::French : Language::English;
            titv::ui::saveLanguage(language_);
            repaint();
            return true;
        case Button::Auto:
            // A click while listening cancels.
            if (engine_->autoLevel().state() == titv::dsp::AutoLevel::State::Listening)
                engine_->autoLevel().cancel();
            else
                engine_->autoLevel().start();
            repaint();
            return true;
        case Button::Undo:
            engine_->requestSlowInputRamp();
            commit(Param::InputGainDb, undoGain_);
            hasUndo_ = false;
            return true;
        case Button::None:
            break;
        }

        const Control* c = controlAt(x, y);
        if (c == nullptr)
            return false;

        const bool doubleClick = c == lastClicked_ && ev.time - lastClickTime_ < kDoubleClickMs;
        lastClicked_ = c;
        lastClickTime_ = ev.time;

        if (c->kind == Kind::Toggle || c->kind == Kind::Led) {
            commit(c->param, value(c->param) >= 0.5f ? 0.0f : 1.0f);
            return true;
        }
        if (doubleClick) {
            commit(c->param, titv::info(c->param).def);
            return true;
        }
        dragging_ = c;
        dragStartY_ = y;
        dragStartValue_ = normalized(c->param, value(c->param));
        dragFine_ = (ev.mod & kModifierShift) != 0;
        editParameter(titv::index(c->param), true);
        return true;
    }

    bool onMotion(const MotionEvent& ev) override
    {
        const View v = view();
        const float x = v.toBaseX(ev.pos.getX()), y = v.toBaseY(ev.pos.getY());

        if (dragging_ != nullptr) {
            // Pressing or releasing Shift mid-drag restarts from the current value.
            const bool fine = (ev.mod & kModifierShift) != 0;
            if (fine != dragFine_) {
                dragFine_ = fine;
                dragStartY_ = y;
                dragStartValue_ = normalized(dragging_->param, value(dragging_->param));
            }
            const float scale = (fine ? kFineDragFactor : 1.0f) / kDragPixelsFullRange;
            const float v = fromNormalized(dragging_->param, dragStartValue_ + (dragStartY_ - y) * scale);
            if (v != value(dragging_->param))
                setValue(dragging_->param, v);
            return true;
        }

        const Control* hovered = controlAt(x, y);
        const Button button = buttonAt(x, y);
        const int row = presetListOpen_ ? presetRowAt(x, y) : -1;
        const bool overDelete = row >= 0 && x >= kPresetX + kPresetW - kDeleteZoneW;
        if (hovered != hovered_ || button != hoveredButton_ || row != hoveredRow_ || overDelete != hoveredDelete_) {
            hovered_ = presetListOpen_ ? nullptr : hovered;
            hoveredButton_ = button;
            hoveredRow_ = row;
            hoveredDelete_ = overDelete;
            repaint();
        }
        return false;
    }

    bool onScroll(const ScrollEvent& ev) override
    {
        if (presetListOpen_) {
            const int maxScroll = std::max(0, static_cast<int>(rows_.size()) - kPresetListMaxRows);
            listScroll_ = std::clamp(listScroll_ + (ev.delta.getY() > 0 ? -1 : 1), 0, maxScroll);
            repaint();
            return true;
        }
        const View placement = view();
        const Control* c = controlAt(placement.toBaseX(ev.pos.getX()), placement.toBaseY(ev.pos.getY()));
        if (c == nullptr || c->kind == Kind::Toggle || c->kind == Kind::Led)
            return false;

        const titv::ParamInfo& pi = titv::info(c->param);
        float v;
        if (pi.kind == titv::ParamKind::Choice) {
            v = value(c->param) + (ev.delta.getY() > 0 ? 1.0f : -1.0f);
        } else {
            const float step = ((ev.mod & kModifierShift) != 0 ? 0.002f : 0.02f) * (pi.max - pi.min);
            v = value(c->param) + static_cast<float>(ev.delta.getY()) * step;
        }
        commit(c->param, titv::Engine::clampToRange(c->param, v));
        return true;
    }

    // Typing a user preset name. Other keys go to the host as usual.
    bool onCharacterInput(const CharacterInputEvent& ev) override
    {
        if (!editingName_ || ev.character < 0x20 || ev.character == 0x7f)
            return false;
        const std::string next = nameText_ + ev.string;
        if (next.size() <= titv::ui::UserPresets::kMaxNameLength)
            nameText_ = next;
        repaint();
        return true;
    }

    bool onKeyboard(const KeyboardEvent& ev) override
    {
        if (!editingName_)
            return false;
        if (ev.press) {
            if (ev.key == kKeyBackspace) {
                // Remove one UTF-8 character.
                while (!nameText_.empty() && (static_cast<unsigned char>(nameText_.back()) & 0xC0) == 0x80)
                    nameText_.pop_back();
                if (!nameText_.empty())
                    nameText_.pop_back();
            } else if (ev.key == kKeyEnter) {
                confirmSave();
            } else if (ev.key == kKeyEscape) {
                editingName_ = false;
            }
            repaint();
        }
        return true; // keep keystrokes away from the host while naming
    }

private:
    // Placement of the base layout in the window: uniform scale, centred.
    struct View {
        float x, y, scale;
        float toBaseX(double px) const { return (static_cast<float>(px) - x) / scale; }
        float toBaseY(double py) const { return (static_cast<float>(py) - y) / scale; }
    };

    View view() const
    {
        const float w = static_cast<float>(getWidth()), h = static_cast<float>(getHeight());
        const float s = std::min(w / kWidth, h / kHeight);
        return { (w - kWidth * s) / 2, (h - kHeight * s) / 2, s };
    }

    void buildLayout()
    {
        sections_.reserve(8);
        controls_.reserve(64); // controls are referenced by pointer once built
        const float gap = 8.0f;
        float x = 16.0f;
        auto addSection = [&](const char* title, float w, Param enable) {
            sections_.push_back({ title, x, w, enable });
            x += w + gap;
            return sections_.back();
        };
        auto knob = [&](Param p, float cx, float cy, float r, Param section) {
            controls_.push_back({ p, info(p).kind == titv::ParamKind::Choice ? Kind::Choice : Kind::Knob,
                                  cx - r, cy - r, 2 * r, 2 * r, section });
        };
        auto grid = [&](const Section& s, std::initializer_list<Param> params) {
            int i = 0;
            for (Param p : params) {
                const float cx = s.x + s.w * (i % 2 == 0 ? 0.28f : 0.72f);
                const float cy = kSectionTop + 86.0f + 94.0f * static_cast<float>(i / 2);
                if (info(p).kind == titv::ParamKind::Toggle)
                    controls_.push_back({ p, Kind::Toggle, cx - 26, cy - 11, 52, 22, s.enable });
                else
                    knob(p, cx, cy, 22.0f, s.enable);
                ++i;
            }
        };
        const Param none = Param::GlobalBypass;

        const Section& in = addSection("INPUT", 110, none);
        knob(Param::InputGainDb, in.x + in.w / 2, kSectionTop + 286, 24, none);

        const Section& comp = addSection("COMPRESS", 150, none);
        knob(Param::CompressAmount, comp.x + comp.w / 2, kSectionTop + 150, 46, none);

        const Section& tone = addSection("TONE", 170, Param::ToneEnabled);
        grid(tone, { Param::ToneBodyDb, Param::ToneMidDb, Param::TonePresenceDb, Param::ToneAirDb });

        const Section& color = addSection("COLOR", 170, Param::ColorEnabled);
        grid(color, { Param::ColorDeess, Param::ColorSaturate, Param::ColorRadio, Param::ColorDouble,
                      Param::ColorChorus });

        const Section& echo = addSection("ECHO", 170, Param::EchoEnabled);
        grid(echo, { Param::EchoSend, Param::EchoRepeats, Param::EchoLofi, Param::EchoNote, Param::EchoBounce });

        const Section& space = addSection("SPACE", 170, Param::SpaceEnabled);
        grid(space, { Param::SpaceRoom, Param::SpacePlate, Param::SpaceHall, Param::SpaceAmbient });
        // Per-reverb enable LEDs, at the top right of each knob.
        const Param leds[] = { Param::SpaceRoomEnabled, Param::SpacePlateEnabled, Param::SpaceHallEnabled,
                               Param::SpaceAmbientEnabled };
        const size_t firstKnob = controls_.size() - 4;
        for (size_t i = 0; i < 4; ++i) {
            const Control k = controls_[firstKnob + i];
            controls_.push_back({ leds[i], Kind::Led, k.x + k.w + 2, k.y - 6, 14, 14, Param::SpaceEnabled });
        }

        const Section& out = addSection("OUTPUT", 110, none);
        knob(Param::OutputGainDb, out.x + out.w / 2, kSectionTop + 286, 24, none);

        // Section enable toggles sit in the section titles.
        for (const Section& s : sections_)
            if (s.enable != none)
                controls_.push_back({ s.enable, Kind::Toggle, s.x + 10, kSectionTop + 8, s.w - 20, 24, none });

        // Global switches in the header.
        controls_.push_back({ Param::HpfEnabled, Kind::Toggle, kWidth - 196.0f, 12, 80, 26, none });
        controls_.push_back({ Param::GlobalBypass, Kind::Toggle, kWidth - 106.0f, 12, 90, 26, none });
    }

    static const titv::ParamInfo& info(Param p) { return titv::info(p); }

    float value(Param p) const { return values_[titv::index(p)]; }

    void setValue(Param p, float v)
    {
        values_[titv::index(p)] = v;
        setParameterValue(titv::index(p), v);
        repaint();
    }

    // A complete gesture, for clicks, double-clicks and scroll steps.
    void commit(Param p, float v)
    {
        editParameter(titv::index(p), true);
        setValue(p, v);
        editParameter(titv::index(p), false);
    }

    bool sectionActive(Param section) const
    {
        return section == Param::GlobalBypass || value(section) >= 0.5f;
    }

    const Control* controlAt(float x, float y) const
    {
        for (const Control& c : controls_)
            if (x >= c.x && x < c.x + c.w && y >= c.y && y < c.y + c.h)
                return &c;
        return nullptr;
    }

    static void meterRect(const Section& s, float& x, float& y, float& w, float& h)
    {
        w = 22.0f;
        h = 190.0f;
        x = s.x + (s.w - w) / 2;
        y = kSectionTop + 44.0f;
    }

    static bool hitMeter(const Section& s, float px, float py)
    {
        float x, y, w, h;
        meterRect(s, x, y, w, h);
        return px >= x && px < x + w && py >= y - 14 && py < y + h;
    }

    static float meterPosition(float linear)
    {
        const float db = titv::dsp::gainToDb(linear);
        return std::clamp((db - kMeterFloorDb) / -kMeterFloorDb, 0.0f, 1.0f);
    }

    void drawHeader()
    {
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(19);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kColors.text);
        text(18, 25, "THIS IS THE VOICE", nullptr);
        if (engine_ != nullptr) {
            char buf[48];
            const Text format = engine_->tempoIsFallback() ? Text::TempoFallback : Text::TempoHost;
            std::snprintf(buf, sizeof(buf), titv::ui::text(format, language_), engine_->tempo());
            fontSize(12);
            fillColor(kColors.textDim);
            textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
            text(kWidth - 212.0f, 25, buf, nullptr);
        }

        beginPath();
        roundedRect(kLangX, kLangY, kLangW, kLangH, kLangH / 2);
        fillColor(kColors.track);
        fill();
        const bool french = language_ == Language::French;
        const float half = kLangW / 2;
        beginPath();
        roundedRect(kLangX + (french ? half : 0.0f), kLangY, half, kLangH, kLangH / 2);
        fillColor(kColors.textDim);
        fill();
        fontSize(11);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(french ? kColors.textDim : kColors.background);
        text(kLangX + half / 2, kLangY + kLangH / 2 + 1, "EN", nullptr);
        fillColor(french ? kColors.background : kColors.textDim);
        text(kLangX + half * 1.5f, kLangY + kLangH / 2 + 1, "FR", nullptr);
    }

    void drawSection(const Section& s)
    {
        beginPath();
        roundedRect(s.x, kSectionTop, s.w, kSectionHeight, 8);
        fillColor(kColors.panel);
        fill();
        strokeColor(kColors.panelEdge);
        strokeWidth(1);
        stroke();

        if (s.enable == Param::GlobalBypass) {
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(13);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(kColors.textDim);
            text(s.x + s.w / 2, kSectionTop + 20, s.title, nullptr);
        }
    }

    void drawMeter(const Section& s, titv::dsp::LevelMeter* meter, bool showTarget)
    {
        float x, y, w, h;
        meterRect(s, x, y, w, h);

        beginPath();
        roundedRect(x, y, w, h, 3);
        fillColor(kColors.track);
        fill();

        if (showTarget) {
            const float top = meterPosition(titv::dsp::dbToGain(kTargetRmsDb + kTargetZoneDb));
            const float bottom = meterPosition(titv::dsp::dbToGain(kTargetRmsDb - kTargetZoneDb));
            beginPath();
            rect(x - 5, y + h * (1 - top), 3, h * (top - bottom));
            fillColor(kColors.textDim);
            fill();
        }

        const bool clipped = meter != nullptr && meter->clipped();
        beginPath();
        circle(x + w / 2, y - 8, 4);
        fillColor(clipped ? kColors.clip : kColors.track);
        fill();

        if (meter == nullptr)
            return;

        const float rms = meterPosition(meter->rms());
        if (rms > 0.0f) {
            beginPath();
            rect(x + 2, y + h * (1 - rms), w - 4, h * rms);
            fillColor(kColors.accent);
            fill();
        }

        const float peak = meterPosition(meter->peak());
        if (peak > 0.0f) {
            beginPath();
            rect(x + 2, y + h * (1 - peak), w - 4, 2);
            fillColor(peak >= 1.0f ? kColors.clip : kColors.text);
            fill();
        }
    }

    void drawControl(const Control& c)
    {
        const bool active = sectionActive(c.section) && value(Param::GlobalBypass) < 0.5f;
        const float v = value(c.param);
        const titv::ParamInfo& pi = info(c.param);

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        globalAlpha(active || c.param == Param::GlobalBypass ? 1.0f : 0.45f);

        switch (c.kind) {
        case Kind::Knob:
        case Kind::Choice:
            drawKnob(c, v, pi);
            if (engine_ != nullptr && c.param == Param::CompressAmount)
                drawReduction(c.x + c.w / 2, c.y + c.h + 44, 100, compressorReduction_, true);
            if (engine_ != nullptr && c.param == Param::ColorDeess)
                drawReduction(c.x + c.w / 2, c.y + c.h + 38, 44, deEsserReduction_, false);
            break;
        case Kind::Toggle: {
            const bool on = v >= 0.5f;
            beginPath();
            roundedRect(c.x, c.y, c.w, c.h, c.h / 2);
            fillColor(on ? kColors.accent : kColors.track);
            fill();
            fontSize(12);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(on ? kColors.background : kColors.textDim);
            text(c.x + c.w / 2, c.y + c.h / 2 + 1, pi.shortName, nullptr);
            break;
        }
        case Kind::Led:
            beginPath();
            circle(c.x + c.w / 2, c.y + c.h / 2, 4.5f);
            fillColor(v >= 0.5f ? kColors.accent : kColors.track);
            fill();
            break;
        }
        globalAlpha(1.0f);
    }

    void drawKnob(const Control& c, float v, const titv::ParamInfo& pi)
    {
        const float cx = c.x + c.w / 2, cy = c.y + c.h / 2, r = c.w / 2;
        const float n = normalized(c.param, v);
        // Bipolar controls (dB around 0) draw their arc from the centre.
        const float origin = pi.min < 0.0f && pi.max > 0.0f ? normalized(c.param, 0.0f) : 0.0f;
        const float a0 = kKnobStart + kKnobSweep * std::min(origin, n);
        const float a1 = kKnobStart + kKnobSweep * std::max(origin, n);
        const float stroke = r > 30 ? 6.0f : 4.0f;

        beginPath();
        arc(cx, cy, r, kKnobStart, kKnobStart + kKnobSweep, CW);
        strokeColor(kColors.track);
        strokeWidth(stroke);
        lineCap(ROUND);
        this->stroke();

        if (a1 - a0 > 0.001f) {
            beginPath();
            arc(cx, cy, r, a0, a1, CW);
            strokeColor(kColors.accent);
            this->stroke();
        }

        const float a = kKnobStart + kKnobSweep * n;
        beginPath();
        moveTo(cx + std::cos(a) * r * 0.25f, cy + std::sin(a) * r * 0.25f);
        lineTo(cx + std::cos(a) * r * 0.8f, cy + std::sin(a) * r * 0.8f);
        strokeColor(kColors.text);
        strokeWidth(2);
        this->stroke();

        char buf[32];
        formatValue(c.param, v, buf, sizeof(buf));
        textAlign(ALIGN_CENTER | ALIGN_TOP);
        fontSize(r > 30 ? 14 : 12);
        fillColor(kColors.text);
        text(cx, cy + r + 8, pi.shortName, nullptr);
        fontSize(11);
        fillColor(&c == dragging_ || &c == hovered_ ? kColors.accent : kColors.textDim);
        text(cx, cy + r + 23, buf, nullptr);
    }

    Button buttonAt(float x, float y) const
    {
        if (inside(x, y, kLangX, kLangY, kLangW, kLangH))
            return Button::Language;
        if (editingName_) {
            if (inside(x, y, kPresetX + kPresetW - 2 * kEditButtonW, kPresetY, kEditButtonW, kPresetH))
                return Button::EditConfirm;
            if (inside(x, y, kPresetX + kPresetW - kEditButtonW, kPresetY, kEditButtonW, kPresetH))
                return Button::EditCancel;
            if (inside(x, y, kPresetX, kPresetY, kPresetW, kPresetH))
                return Button::PresetName;
        }
        if (inside(x, y, kPresetX, kPresetY, kPresetArrowW, kPresetH))
            return Button::PresetPrevious;
        if (inside(x, y, kPresetX + kPresetW - kPresetArrowW, kPresetY, kPresetArrowW, kPresetH))
            return Button::PresetNext;
        if (inside(x, y, kPresetX, kPresetY, kPresetW, kPresetH))
            return Button::PresetName;
        if (engine_ == nullptr)
            return Button::None;
        if (inside(x, y, kAutoX, kAutoY, kAutoW, kAutoH))
            return Button::Auto;
        if (hasUndo_ && inside(x, y, kUndoX, kAutoY, kUndoW, kAutoH))
            return Button::Undo;
        return Button::None;
    }

    // --- Presets -------------------------------------------------------------

    void openPresetList()
    {
        presets_.refresh();
        rows_.clear();
        const auto& entries = presets_.entries();
        for (size_t i = 0; i < entries.size(); ++i) {
            if (i == presets_.factoryCount())
                rows_.push_back({ PresetRow::UserHeader });
            rows_.push_back({ PresetRow::Entry, i });
        }
        rows_.push_back({ PresetRow::SaveAction });
        // Show the current preset.
        const int current = presets_.currentIndex();
        int currentRow = 0;
        for (size_t r = 0; r < rows_.size(); ++r)
            if (rows_[r].kind == PresetRow::Entry && static_cast<int>(rows_[r].entry) == current)
                currentRow = static_cast<int>(r);
        const int maxScroll = std::max(0, static_cast<int>(rows_.size()) - kPresetListMaxRows);
        listScroll_ = std::clamp(currentRow - kPresetListMaxRows / 2, 0, maxScroll);
        pendingDelete_.clear();
        presetListOpen_ = true;
        repaint();
    }

    int visibleRows() const { return std::min(static_cast<int>(rows_.size()), kPresetListMaxRows); }

    // Row under a point, as an index in rows_, or -1.
    int presetRowAt(float x, float y) const
    {
        if (x < kPresetX || x >= kPresetX + kPresetW || y < kPresetListTop)
            return -1;
        const int visible = static_cast<int>((y - kPresetListTop) / kPresetItemH);
        return visible < visibleRows() ? visible + listScroll_ : -1;
    }

    void onPresetListClick(float x, float y)
    {
        const int r = presetRowAt(x, y);
        if (r < 0) {
            presetListOpen_ = false;
            return;
        }
        const PresetRow& row = rows_[static_cast<size_t>(r)];
        if (row.kind == PresetRow::UserHeader)
            return;
        if (row.kind == PresetRow::SaveAction) {
            presetListOpen_ = false;
            startNaming();
            return;
        }
        const auto entry = presets_.entries()[row.entry];
        if (entry.user && x >= kPresetX + kPresetW - kDeleteZoneW) {
            // First click asks, second click deletes.
            if (pendingDelete_ == entry.name) {
                presets_.removeUser(entry.name);
                openPresetList();
            } else {
                pendingDelete_ = entry.name;
            }
            return;
        }
        presetListOpen_ = false;
        applyEntry(entry);
    }

    void startNaming()
    {
        const auto& current = presets_.current();
        nameText_ = current.user ? current.name : std::string();
        editingName_ = true;
        getWindow().focus(); // keystrokes come to the editor
    }

    void confirmSave()
    {
        const std::string name = titv::ui::UserPresets::cleanName(nameText_);
        if (name.empty())
            return; // keep the field open until there is a name
        if (presets_.saveUser(name, values_))
            setState("preset", presets_.stateValue().c_str());
        editingName_ = false;
    }

    // Writes every value the preset controls as one host gesture per parameter,
    // all begun before any value changes, then records the choice in the state.
    void applyEntry(const titv::ui::PresetBrowser::Entry& entry)
    {
        const auto target = presets_.valuesOf(entry);
        if (!target)
            return; // unreadable user file: leave everything as it is
        std::array<bool, titv::kParamCount> changing {};
        for (uint32_t p = 0; p < titv::kParamCount; ++p)
            changing[p] = titv::presetControls(static_cast<Param>(p)) && (*target)[p] != values_[p];
        for (uint32_t p = 0; p < titv::kParamCount; ++p)
            if (changing[p])
                editParameter(p, true);
        for (uint32_t p = 0; p < titv::kParamCount; ++p)
            if (changing[p]) {
                values_[p] = (*target)[p];
                setParameterValue(p, (*target)[p]);
            }
        for (uint32_t p = 0; p < titv::kParamCount; ++p)
            if (changing[p])
                editParameter(p, false);
        presets_.select(entry, *target);
        setState("preset", presets_.stateValue().c_str());
        repaint();
    }

    void drawPresetSelector()
    {
        beginPath();
        roundedRect(kPresetX, kPresetY, kPresetW, kPresetH, kPresetH / 2);
        fillColor(editingName_ ? kColors.panel : kColors.track);
        fill();
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const float cy = kPresetY + kPresetH / 2 + 1;

        if (editingName_) {
            strokeColor(kColors.accent);
            strokeWidth(1);
            stroke();
            fontSize(12);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            const float tx = kPresetX + 14;
            if (nameText_.empty()) {
                fillColor(kColors.textDim);
                text(tx, cy, titv::ui::text(Text::PresetNamePlaceholder, language_), nullptr);
            } else {
                fillColor(kColors.text);
                text(tx, cy, nameText_.c_str(), nullptr);
            }
            Rectangle<float> bounds;
            const float advance = nameText_.empty() ? 0.0f : textBounds(tx, cy, nameText_.c_str(), nullptr, bounds);
            beginPath();
            rect(tx + advance + 1, kPresetY + 5, 1.5f, kPresetH - 10);
            fillColor(kColors.accent);
            fill();
            // Confirm and cancel.
            fontSize(14);
            textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
            fillColor(hoveredButton_ == Button::EditConfirm ? kColors.accent : kColors.text);
            text(kPresetX + kPresetW - 1.5f * kEditButtonW, cy, "\u2713", nullptr);
            fillColor(hoveredButton_ == Button::EditCancel ? kColors.text : kColors.textDim);
            text(kPresetX + kPresetW - 0.5f * kEditButtonW, cy, "\u2715", nullptr);
            return;
        }

        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fontSize(20);
        fillColor(hoveredButton_ == Button::PresetPrevious ? kColors.text : kColors.textDim);
        text(kPresetX + kPresetArrowW / 2, cy, "\u2039", nullptr);
        fillColor(hoveredButton_ == Button::PresetNext ? kColors.text : kColors.textDim);
        text(kPresetX + kPresetW - kPresetArrowW / 2, cy, "\u203a", nullptr);

        const std::string label = presets_.displayName(presets_.current(), language_) +
                                  (presets_.isModified(values_) ? " *" : "");
        fontSize(12);
        fillColor(kColors.text);
        text(kPresetX + kPresetW / 2, cy, label.c_str(), nullptr);
    }

    void drawPresetList()
    {
        if (!presetListOpen_)
            return;
        const int visible = visibleRows();
        const float h = kPresetItemH * static_cast<float>(visible);
        beginPath();
        roundedRect(kPresetX, kPresetListTop, kPresetW, h + 4, 6);
        fillColor(kColors.panel);
        fill();
        strokeColor(kColors.panelEdge);
        strokeWidth(1);
        stroke();

        fontFace(NANOVG_DEJAVU_SANS_TTF);
        const int current = presets_.currentIndex();
        for (int v = 0; v < visible; ++v) {
            const int r = v + listScroll_;
            const PresetRow& row = rows_[static_cast<size_t>(r)];
            const float y = kPresetListTop + 2 + kPresetItemH * static_cast<float>(v);
            const float cy = y + kPresetItemH / 2 + 1;

            if (row.kind == PresetRow::UserHeader) {
                fontSize(10);
                textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
                fillColor(kColors.textDim);
                text(kPresetX + 14, cy, titv::ui::text(Text::UserPresetsHeader, language_), nullptr);
                beginPath();
                rect(kPresetX + 110, cy, kPresetW - 124, 1);
                fillColor(kColors.panelEdge);
                fill();
                continue;
            }
            if (r == hoveredRow_) {
                beginPath();
                rect(kPresetX + 1, y, kPresetW - 2, kPresetItemH);
                fillColor(kColors.track);
                fill();
            }
            fontSize(12);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            if (row.kind == PresetRow::SaveAction) {
                fillColor(r == hoveredRow_ ? kColors.accent : kColors.text);
                text(kPresetX + 14, cy, titv::ui::text(Text::SavePreset, language_), nullptr);
                continue;
            }
            const auto& entry = presets_.entries()[row.entry];
            const bool isCurrent = static_cast<int>(row.entry) == current;
            if (isCurrent) {
                beginPath();
                circle(kPresetX + 14, cy - 1, 3);
                fillColor(kColors.accent);
                fill();
            }
            fillColor(isCurrent ? kColors.text : kColors.textDim);
            text(kPresetX + 26, cy, presets_.displayName(entry, language_).c_str(), nullptr);
            if (entry.user && (r == hoveredRow_ || pendingDelete_ == entry.name)) {
                const bool asking = pendingDelete_ == entry.name;
                textAlign(ALIGN_RIGHT | ALIGN_MIDDLE);
                fontSize(asking ? 11 : 14);
                fillColor(asking ? kColors.clip : (hoveredDelete_ ? kColors.text : kColors.textDim));
                text(kPresetX + kPresetW - 12, cy, asking ? titv::ui::text(Text::DeletePresetConfirm, language_) : "\u00d7",
                     nullptr);
            }
        }
        // More rows than fit: a thin position bar.
        if (static_cast<int>(rows_.size()) > visible) {
            const float share = static_cast<float>(visible) / static_cast<float>(rows_.size());
            const float pos = static_cast<float>(listScroll_) / static_cast<float>(rows_.size());
            beginPath();
            rect(kPresetX + kPresetW - 4, kPresetListTop + 2 + h * pos, 2, h * share);
            fillColor(kColors.textDim);
            fill();
        }
    }

    void drawAutoLevel()
    {
        if (engine_ == nullptr)
            return;
        const titv::dsp::AutoLevel& autoLevel = engine_->autoLevel();
        const bool listening = autoLevel.state() == titv::dsp::AutoLevel::State::Listening;

        beginPath();
        roundedRect(kAutoX, kAutoY, kAutoW, kAutoH, kAutoH / 2);
        fillColor(kColors.track);
        fill();

        char label[16];
        if (listening) {
            // The bar only advances while voice is heard.
            const float p = autoLevel.progress();
            if (p > 0.0f) {
                beginPath();
                roundedRect(kAutoX, kAutoY, std::max(kAutoH, kAutoW * p), kAutoH, kAutoH / 2);
                fillColor(kColors.accent);
                fill();
            }
            std::snprintf(label, sizeof(label), "%.0f %%", p * 100.0f);
        } else {
            std::snprintf(label, sizeof(label), "AUTO");
        }
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(10);
        textAlign(ALIGN_CENTER | ALIGN_MIDDLE);
        fillColor(listening ? kColors.text : kColors.textDim);
        text(kAutoX + kAutoW / 2, kAutoY + kAutoH / 2 + 1, label, nullptr);

        if (hasUndo_ && !listening) {
            beginPath();
            roundedRect(kUndoX, kAutoY, kUndoW, kAutoH, kAutoH / 2);
            fillColor(kColors.track);
            fill();
            fontSize(12);
            fillColor(kColors.textDim);
            text(kUndoX + kUndoW / 2, kAutoY + kAutoH / 2 + 1, "\u21ba", nullptr);
        }
    }

    // Horizontal gain-reduction bar, filling from the right as reduction grows.
    // Ducking activity of the ECHO and SPACE returns, at the bottom of both sections.
    void drawDucking()
    {
        if (engine_ == nullptr)
            return;
        for (const Section& s : sections_) {
            if (s.enable != Param::EchoEnabled && s.enable != Param::SpaceEnabled)
                continue;
            globalAlpha(sectionActive(s.enable) ? 1.0f : 0.45f);
            const float y = kSectionTop + kSectionHeight - 22.0f;
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(10);
            textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
            fillColor(kColors.textDim);
            text(s.x + 12, y + 2, "DUCK", nullptr);
            // Full bar: voice fully present, returns at their deepest dip.
            drawReduction(s.x + s.w / 2 + 18, y, s.w - 76, duckActivity_ * kReductionRangeDb, false);
            globalAlpha(1.0f);
        }
    }

    void drawReduction(float cx, float y, float w, float reductionDb, bool withValue)
    {
        const float x = cx - w / 2, h = 4.0f;
        beginPath();
        roundedRect(x, y, w, h, 2);
        fillColor(kColors.track);
        fill();
        const float amount = std::clamp(reductionDb / kReductionRangeDb, 0.0f, 1.0f);
        if (amount > 0.0f) {
            beginPath();
            roundedRect(x + w * (1 - amount), y, w * amount, h, 2);
            fillColor(kColors.accent);
            fill();
        }
        if (withValue) {
            char buf[24];
            std::snprintf(buf, sizeof(buf), "GR %.1f dB", reductionDb >= 0.05f ? -reductionDb : 0.0f);
            fontFace(NANOVG_DEJAVU_SANS_TTF);
            fontSize(10);
            textAlign(ALIGN_CENTER | ALIGN_TOP);
            fillColor(kColors.textDim);
            text(cx, y + 8, buf, nullptr);
        }
    }

    void drawHelp()
    {
        const Control* c = dragging_ != nullptr ? dragging_ : hovered_;
        const char* help = nullptr;
        if (c != nullptr)
            help = titv::ui::helpText(c->param, language_);
        else if (editingName_)
            help = titv::ui::text(presets_.userExists(nameText_) ? Text::SavePresetReplaceHelp : Text::SavePresetHelp,
                                  language_);
        else if (presetListOpen_ && hoveredDelete_ && hoveredRow_ >= 0 &&
                 rows_[static_cast<size_t>(hoveredRow_)].kind == PresetRow::Entry &&
                 presets_.entries()[rows_[static_cast<size_t>(hoveredRow_)].entry].user)
            help = titv::ui::text(Text::DeletePresetHelp, language_);
        else if (hoveredButton_ == Button::PresetPrevious || hoveredButton_ == Button::PresetNext ||
                 hoveredButton_ == Button::PresetName || presetListOpen_)
            help = titv::ui::text(Text::PresetHelp, language_);
        else if (hoveredButton_ == Button::Language)
            help = titv::ui::text(Text::LanguageHelp, language_);
        else if (hoveredButton_ == Button::Auto)
            help = titv::ui::text(Text::AutoLevelHelp, language_);
        else if (hoveredButton_ == Button::Undo)
            help = titv::ui::text(Text::AutoLevelUndoHelp, language_);
        if (help == nullptr)
            return;
        fontFace(NANOVG_DEJAVU_SANS_TTF);
        fontSize(13);
        textAlign(ALIGN_LEFT | ALIGN_MIDDLE);
        fillColor(kColors.textDim);
        text(18, kHelpTop + 18, help, nullptr);
    }

    titv::Engine* engine_ = nullptr;
    std::array<float, titv::kParamCount> values_ {};
    std::vector<Section> sections_;
    std::vector<Control> controls_;

    Language language_ = Language::English;
    titv::ui::PresetBrowser presets_;
    std::vector<PresetRow> rows_;
    bool presetListOpen_ = false;
    int listScroll_ = 0;
    int hoveredRow_ = -1;
    bool hoveredDelete_ = false;
    std::string pendingDelete_;
    bool editingName_ = false;
    std::string nameText_;
    Button hoveredButton_ = Button::None;
    float undoGain_ = 0.0f;
    bool hasUndo_ = false;
    float compressorReduction_ = 0.0f, deEsserReduction_ = 0.0f;
    float duckActivity_ = 0.0f;
    const Control* hovered_ = nullptr;
    const Control* dragging_ = nullptr;
    const Control* lastClicked_ = nullptr;
    uint lastClickTime_ = 0;
    float dragStartY_ = 0.0f;
    float dragStartValue_ = 0.0f;
    bool dragFine_ = false;

    DISTRHO_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VoiceUI)
};

UI* createUI()
{
    return new VoiceUI();
}

END_NAMESPACE_DISTRHO
