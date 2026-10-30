#pragma once

#include <JuceHeader.h>
#include "TrackRack.h"
#include <atomic>

//==============================================================================
// The drum synth: eight drums on fixed notes, each with Tune, Decay, Tone and
// Level, plus Drive and Dust for the whole kit.
//
// Each drum is synthesised from its knobs into a short one-shot (the same
// recipes as the generated breaks in BreakLibrary: swept sine kick, body +
// noise snare, burst clap, 808-style metallic hats) and the audio thread just
// plays those back. A hit costs nothing, and turning a knob re-renders that
// one drum (a few ms) on the message thread. Three takes of each drum with
// different noise play in turn, so a hat roll doesn't machine-gun.
namespace Drums
{
    enum Id { Kick = 0, Snare, Clap, ClosedHat, OpenHat, Tom, Rim, Perc, NumDrums };

    inline const juce::StringArray& names()
    {
        static const juce::StringArray n { "Kick", "Snare", "Clap", "Closed Hat", "Open Hat", "Tom", "Rim", "Perc" };
        return n;
    }

    inline const juce::StringArray& shortNames()
    {
        static const juce::StringArray n { "KICK", "SNARE", "CLAP", "C.HAT", "O.HAT", "TOM", "RIM", "PERC" };
        return n;
    }

    // General MIDI drum notes, so a DAW's drum map lines up too.
    inline int noteFor (int drum)
    {
        static const int notes[NumDrums] = { 36, 38, 39, 42, 46, 45, 37, 56 };
        return notes[juce::jlimit (0, NumDrums - 1, drum)];
    }

    // Which drum a note plays, and how far to retune it (the GM toms are one
    // drum at different pitches). Notes outside the GM map use the layout of
    // the octave from C2, so any controller plays something.
    inline int drumForNote (int note, float& semitones)
    {
        semitones = 0.0f;
        switch (note)
        {
            case 35: case 36: return Kick;
            case 37:          return Rim;
            case 38: case 40: return Snare;
            case 39:          return Clap;
            case 42: case 44: return ClosedHat;
            case 46: case 49: return OpenHat;
            case 41: semitones = -5.0f; return Tom;
            case 43: semitones = -3.0f; return Tom;
            case 45:                    return Tom;
            case 47: semitones =  2.0f; return Tom;
            case 48: semitones =  4.0f; return Tom;
            case 50: semitones =  7.0f; return Tom;
            case 56: case 51: case 54: return Perc;
            default: break;
        }
        static const int octave[12] = { Kick, Rim, Snare, Clap, Snare, Tom, ClosedHat, Tom, ClosedHat, Tom, OpenHat, Perc };
        static const float tomSemis[12] = { 0, 0, 0, 0, 0, -5, 0, -3, 0, 0, 0, 0 };
        const int pc = ((note % 12) + 12) % 12;
        semitones = tomSemis[pc];
        return octave[pc];
    }

    struct Params
    {
        float tune  = 0.0f;   // semitones, -12..+12
        float decay = 1.0f;   // x the drum's own decay, 0.25..4
        float tone  = 0.5f;   // 0..1, means something per drum (see render)
        float level = 0.8f;   // 0..1.5
    };

    struct Kit
    {
        juce::String name;
        float drive = 0.2f, dust = 0.0f;   // 0..1
        std::array<Params, NumDrums> drums;
    };

    //==========================================================================
    // Rendering — message thread, any sample rate.
    namespace detail
    {
        constexpr double twoPi = juce::MathConstants<double>::twoPi;

        struct OnePole   // low-pass; high-pass is input minus this
        {
            float a = 0.0f, z = 0.0f;
            void setCutoff (double hz, double fs) { a = (float) std::exp (-twoPi * juce::jmin (hz, fs * 0.45) / fs); }
            float lp (float x) { z = x + a * (z - x); return z; }
            float hp (float x) { return x - lp (x); }
        };

        // Six detuned squares, the 808's metal.
        struct Metal
        {
            double phase[6] {}, inc[6] {};
            Metal (double scale, double fs)
            {
                static const double freqs[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                for (int i = 0; i < 6; ++i)
                    inc[i] = freqs[i] * scale / fs;
            }
            float next()
            {
                float s = 0.0f;
                for (int i = 0; i < 6; ++i)
                {
                    s += phase[i] < 0.5 ? 1.0f : -1.0f;
                    phase[i] += inc[i];
                    if (phase[i] >= 1.0) phase[i] -= 1.0;
                }
                return s / 6.0f;
            }
        };

        inline float noise (juce::Random& r) { return r.nextFloat() * 2.0f - 1.0f; }

        // One drum, one take. Peaks around 1 before Level.
        inline std::vector<float> render (int drum, const Params& p, double fs, juce::Random& rng)
        {
            const double tr = std::pow (2.0, p.tune / 12.0);
            const double d  = juce::jlimit (0.25, 4.0, (double) p.decay);
            const float tone = juce::jlimit (0.0f, 1.0f, p.tone);

            // The longest any drum rings: its decay constant times six, capped.
            auto lengthFor = [&] (double tau) { return (int) (juce::jmin (4.0, tau * 6.0 + 0.01) * fs); };
            std::vector<float> out;

            switch (drum)
            {
                case Kick:   // tone: click and how far the pitch drops
                {
                    const double tau = 0.30 * d, high = (90.0 + 140.0 * tone) * tr, low = 50.0 * tr;
                    out.resize ((size_t) lengthFor (tau));
                    double phase = 0.0;
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        phase += twoPi * (low + (high - low) * std::exp (-t / 0.03)) / fs;
                        const float body  = (float) (std::sin (phase) * std::exp (-t / tau));
                        const float click = noise (rng) * (float) std::exp (-t / 0.0015) * (0.1f + 0.5f * tone);
                        out[i] = std::tanh (1.4f * (body + click));
                    }
                    break;
                }

                case Snare:   // tone: snappy (noise) against body
                {
                    const double tau = 0.16 * d, f = 190.0 * tr;
                    out.resize ((size_t) lengthFor (tau));
                    OnePole hp, lp;
                    hp.setCutoff (1200.0, fs);
                    lp.setCutoff (8000.0, fs);
                    const float bodyAmt = 0.9f - 0.6f * tone, noiseAmt = 0.6f + 1.0f * tone;
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        const float body = (float) ((std::sin (twoPi * f * t) * 0.6 + std::sin (twoPi * f * 1.74 * t) * 0.4)
                                                    * std::exp (-t / (0.055 * std::sqrt (d))));
                        const float n = lp.lp (hp.hp (noise (rng))) * (float) std::exp (-t / tau);
                        out[i] = body * bodyAmt + n * noiseAmt;
                    }
                    break;
                }

                case Clap:   // tone: where the band sits
                {
                    const double tau = 0.11 * d;
                    out.resize ((size_t) lengthFor (tau));
                    OnePole hp, lp;
                    hp.setCutoff ((500.0 + 900.0 * tone) * tr, fs);
                    lp.setCutoff ((1600.0 + 3000.0 * tone) * tr, fs);
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        double env = std::exp (-juce::jmax (0.0, t - 0.02) / tau) * 0.6;
                        for (double burst : { 0.0, 0.009, 0.018 })
                            if (t >= burst)
                                env += std::exp (-(t - burst) / 0.004);
                        out[i] = 1.4f * lp.lp (hp.hp (noise (rng))) * (float) env;
                    }
                    break;
                }

                case ClosedHat:
                case OpenHat:   // tone: brightness
                {
                    const double tau = (drum == ClosedHat ? 0.045 : 0.33) * d;
                    out.resize ((size_t) lengthFor (tau));
                    Metal metal (tr, fs);
                    OnePole hp1, hp2;
                    const double hpHz = 4500.0 + 5000.0 * tone;
                    hp1.setCutoff (hpHz, fs);
                    hp2.setCutoff (hpHz, fs);
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        const float s = hp2.hp (hp1.hp (metal.next() + noise (rng) * 0.15f));
                        out[i] = 2.6f * s * (float) std::exp (-t / tau);
                    }
                    break;
                }

                case Tom:   // tone: stick noise on the attack
                {
                    const double tau = 0.25 * d, f = 110.0 * tr;
                    out.resize ((size_t) lengthFor (tau));
                    double phase = 0.0;
                    OnePole lp;
                    lp.setCutoff (3000.0, fs);
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        phase += twoPi * f * (1.0 + 0.6 * std::exp (-t / 0.04)) / fs;
                        const float body = (float) (std::sin (phase) * std::exp (-t / tau));
                        const float stick = lp.lp (noise (rng)) * (float) std::exp (-t / 0.012) * tone * 1.5f;
                        out[i] = std::tanh (1.2f * (body + stick));
                    }
                    break;
                }

                case Rim:   // tone: wood against click
                {
                    const double tau = 0.03 * d;
                    out.resize ((size_t) lengthFor (tau));
                    OnePole hp;
                    hp.setCutoff (2000.0, fs);
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        const double env = std::exp (-t / tau);
                        const float wood = (float) ((std::sin (twoPi * 480.0 * tr * t) * 0.7 + std::sin (twoPi * 1650.0 * tr * t) * 0.5) * env);
                        const float click = hp.hp (noise (rng)) * (float) std::exp (-t / 0.003);
                        out[i] = wood * (1.0f - 0.5f * tone) + click * (0.3f + 1.2f * tone);
                    }
                    break;
                }

                case Perc:   // a cowbell; tone: brightness
                {
                    const double tau = 0.12 * d;
                    out.resize ((size_t) lengthFor (tau));
                    double ph1 = 0.0, ph2 = 0.0;
                    const double i1 = 540.0 * tr / fs, i2 = 800.0 * tr / fs;
                    OnePole hp, lp;
                    hp.setCutoff (400.0, fs);
                    lp.setCutoff (2500.0 + 6000.0 * tone, fs);
                    for (size_t i = 0; i < out.size(); ++i)
                    {
                        const double t = (double) i / fs;
                        const float sq = (ph1 < 0.5 ? 1.0f : -1.0f) + (ph2 < 0.5 ? 1.0f : -1.0f);
                        ph1 += i1; if (ph1 >= 1.0) ph1 -= 1.0;
                        ph2 += i2; if (ph2 >= 1.0) ph2 -= 1.0;
                        const double env = std::exp (-t / 0.015) * 0.5 + std::exp (-t / tau) * 0.5;
                        out[i] = 0.9f * lp.lp (hp.hp (sq * 0.5f)) * (float) env;
                    }
                    break;
                }

                default: break;
            }

            // A short fade at the end, so the cut never clicks.
            const int fade = juce::jmin ((int) out.size(), (int) (0.005 * fs));
            for (int i = 0; i < fade; ++i)
                out[out.size() - 1 - (size_t) i] *= (float) i / (float) fade;
            return out;
        }

        // The kit's Drive and Dust, then Level.
        inline void finish (std::vector<float>& s, const Kit& kit, float level, double fs)
        {
            const float g = 1.0f + 5.0f * kit.drive;
            const float norm = 1.0f / std::tanh (g);
            OnePole lp;
            lp.setCutoff (18000.0 * std::pow (0.12, (double) kit.dust), fs);
            const float steps = kit.dust > 0.5f ? std::pow (2.0f, 12.0f - 14.0f * (kit.dust - 0.5f)) : 0.0f;
            for (auto& x : s)
            {
                float y = kit.drive > 0.0f ? std::tanh (g * x) * norm : x;
                if (kit.dust > 0.0f) y = lp.lp (y);
                if (steps > 0.0f) y = std::round (y * steps) / steps;
                x = y * level;
            }
        }
    }

    //==========================================================================
    // Factory kits. Params: tune, decay, tone, level.
    inline const std::vector<Kit>& factoryKits()
    {
        static const std::vector<Kit> kits = [] {
            auto kit = [] (const char* name, float drive, float dust, std::array<Params, NumDrums> p) { return Kit { name, drive, dust, p }; };
            return std::vector<Kit> {
                kit ("808", 0.15f, 0.0f, {{ { -2, 2.4f, 0.35f, 1.0f }, { 0, 1.0f, 0.55f, 0.75f }, { 0, 1.1f, 0.45f, 0.75f },
                                            { 0, 1.0f, 0.55f, 0.55f }, { 0, 1.2f, 0.55f, 0.5f }, { -2, 1.4f, 0.2f, 0.8f },
                                            { 0, 1.0f, 0.5f, 0.6f }, { 0, 1.0f, 0.5f, 0.55f } }}),
                kit ("Boom Bap", 0.45f, 0.35f, {{ { -1, 1.3f, 0.6f, 1.0f }, { -2, 1.3f, 0.45f, 0.95f }, { 0, 1.0f, 0.4f, 0.7f },
                                                  { -1, 0.9f, 0.4f, 0.5f }, { -1, 0.8f, 0.4f, 0.45f }, { -3, 1.0f, 0.4f, 0.7f },
                                                  { -2, 1.0f, 0.3f, 0.6f }, { -2, 0.8f, 0.3f, 0.45f } }}),
                kit ("Jungle", 0.35f, 0.15f, {{ { 2, 0.8f, 0.7f, 0.95f }, { 3, 0.8f, 0.6f, 1.0f }, { 2, 0.8f, 0.6f, 0.65f },
                                                { 1, 0.8f, 0.6f, 0.55f }, { 1, 0.6f, 0.6f, 0.45f }, { 2, 0.9f, 0.5f, 0.7f },
                                                { 2, 0.8f, 0.6f, 0.65f }, { 1, 0.8f, 0.6f, 0.5f } }}),
                kit ("House", 0.25f, 0.0f, {{ { 0, 1.2f, 0.5f, 1.0f }, { 1, 0.9f, 0.6f, 0.7f }, { 0, 1.2f, 0.55f, 0.85f },
                                              { 0, 0.9f, 0.7f, 0.6f }, { 0, 1.0f, 0.7f, 0.55f }, { 0, 1.0f, 0.3f, 0.7f },
                                              { 0, 1.0f, 0.6f, 0.6f }, { 0, 1.0f, 0.6f, 0.5f } }}),
                kit ("Trap", 0.3f, 0.0f, {{ { -5, 3.5f, 0.25f, 1.0f }, { 2, 0.8f, 0.75f, 0.8f }, { 1, 1.0f, 0.6f, 0.9f },
                                            { 2, 0.7f, 0.8f, 0.55f }, { 1, 0.8f, 0.8f, 0.45f }, { -4, 1.6f, 0.2f, 0.8f },
                                            { 3, 0.8f, 0.7f, 0.65f }, { 2, 0.8f, 0.6f, 0.5f } }}),
                kit ("Lo-fi", 0.55f, 0.75f, {{ { -2, 1.1f, 0.4f, 1.0f }, { -3, 1.2f, 0.4f, 0.9f }, { -2, 1.0f, 0.3f, 0.7f },
                                               { -3, 0.9f, 0.3f, 0.55f }, { -3, 0.8f, 0.3f, 0.45f }, { -4, 1.0f, 0.3f, 0.7f },
                                               { -3, 1.0f, 0.3f, 0.6f }, { -4, 0.9f, 0.3f, 0.45f } }}),
                kit ("Disco", 0.2f, 0.1f, {{ { 0, 1.0f, 0.5f, 0.95f }, { 0, 1.1f, 0.5f, 0.85f }, { 0, 1.0f, 0.5f, 0.75f },
                                             { 0, 1.1f, 0.6f, 0.6f }, { 0, 1.3f, 0.6f, 0.55f }, { 2, 1.2f, 0.4f, 0.75f },
                                             { 0, 1.0f, 0.5f, 0.6f }, { 1, 1.0f, 0.5f, 0.55f } }}),
                kit ("Techno", 0.6f, 0.05f, {{ { -1, 1.6f, 0.45f, 1.0f }, { 0, 0.8f, 0.7f, 0.7f }, { -1, 1.4f, 0.4f, 0.8f },
                                               { 0, 0.8f, 0.8f, 0.6f }, { 0, 1.4f, 0.75f, 0.55f }, { -3, 1.2f, 0.3f, 0.7f },
                                               { 0, 0.8f, 0.7f, 0.6f }, { -2, 1.2f, 0.5f, 0.5f } }}),
            };
        }();
        return kits;
    }

    inline const Kit* findFactoryKit (const juce::String& name)
    {
        for (auto& k : factoryKits())
            if (k.name == name)
                return &k;
        return nullptr;
    }
}

//==============================================================================
class DrumSource : public SoundSource,
                   private juce::Timer
{
public:
    static constexpr int NumTakes = 3, Polyphony = 24, NumSlots = Polyphony + 8, FadeSamples = 128;

    DrumSource()  { loadKit (Drums::factoryKits().front()); }
    ~DrumSource() override
    {
        stopTimer();
        for (auto& r : rendered) delete r.load();
        for (auto& old : retired) delete old.first;
    }

    //==========================================================================
    // The kit — message thread.
    void loadKit (const Drums::Kit& k)
    {
        kit = k;
        for (int d = 0; d < Drums::NumDrums; ++d)
            renderDrum (d);
    }

    const Drums::Kit& getKit() const noexcept { return kit; }
    juce::String getKitName() const           { return kit.name; }

    enum Param { Tune, Decay, Tone, Level };

    void setParam (int drum, Param p, float value)
    {
        if (! juce::isPositiveAndBelow (drum, (int) Drums::NumDrums)) return;
        auto& d = kit.drums[(size_t) drum];
        switch (p)
        {
            case Tune:  d.tune  = juce::jlimit (-12.0f, 12.0f, value); break;
            case Decay: d.decay = juce::jlimit (0.25f, 4.0f, value);   break;
            case Tone:  d.tone  = juce::jlimit (0.0f, 1.0f, value);    break;
            case Level: d.level = juce::jlimit (0.0f, 1.5f, value);    break;
        }
        dirty[(size_t) drum] = true;
        if (! isTimerRunning()) startTimerHz (30);   // re-render at most 30 times a second while dragging
    }

    // Drive and Dust touch every drum.
    void setDrive (float v) { kit.drive = juce::jlimit (0.0f, 1.0f, v); markAllDirty(); }
    void setDust (float v)  { kit.dust  = juce::jlimit (0.0f, 1.0f, v); markAllDirty(); }

    // Renders anything still waiting — after a drag, or before a test listens.
    void flush()
    {
        for (int d = 0; d < Drums::NumDrums; ++d)
            if (std::exchange (dirty[(size_t) d], false))
                renderDrum (d);
        freeRetired (false);
    }

    //==========================================================================
    // SoundSource
    juce::String typeId() const override { return "drums"; }

    void prepare (double sampleRate, int) override { playbackStep = RenderRate / sampleRate; }

    void render (juce::AudioBuffer<float>& buffer, int start, int num) override
    {
        if (releasePending.exchange (false))
            for (auto& v : voices)
                if (v.active) v.fade = juce::jmin (v.fade < 0 ? FadeSamples : v.fade, FadeSamples);

        float* L = buffer.getWritePointer (0, start);
        float* R = buffer.getNumChannels() > 1 ? buffer.getWritePointer (1, start) : nullptr;
        juce::FloatVectorOperations::clear (L, num);
        if (R != nullptr) juce::FloatVectorOperations::clear (R, num);

        for (auto& v : voices)
        {
            if (! v.active) continue;
            const auto& s = *v.sample;
            const int len = (int) s.size();
            for (int i = 0; i < num; ++i)
            {
                const int idx = (int) v.pos;
                if (idx + 1 >= len) { v.active = false; break; }
                const float frac = (float) (v.pos - idx);
                float x = (s[(size_t) idx] + frac * (s[(size_t) idx + 1] - s[(size_t) idx])) * v.gain;
                if (v.fade >= 0)
                {
                    if (v.fade == 0) { v.active = false; break; }
                    x *= (float) v.fade-- / (float) FadeSamples;
                }
                L[i] += x;
                v.pos += v.step;
            }
        }

        if (R != nullptr)
            juce::FloatVectorOperations::copy (R, L, num);
    }

    void noteOnAudio (int, int note, int velocity127) override
    {
        float semis = 0.0f;
        const int drum = Drums::drumForNote (note, semis);
        const auto* r = rendered[(size_t) drum].load();
        if (r == nullptr || velocity127 <= 0) return;

        // Hats choke the open hat, as on the machines.
        if (drum == Drums::ClosedHat || drum == Drums::OpenHat)
            for (auto& v : voices)
                if (v.active && v.drum == Drums::OpenHat && v.fade < 0)
                    v.fade = FadeSamples;

        auto& v = freeVoice();
        const int take = nextTake[(size_t) drum];
        nextTake[(size_t) drum] = (take + 1) % NumTakes;
        v.sample = &r->takes[(size_t) take];
        v.pos    = 0.0;
        v.step   = playbackStep * std::pow (2.0, semis / 12.0);
        const float vel = (float) velocity127 / 127.0f;
        v.gain   = OutputGain * vel * std::sqrt (vel);
        v.drum   = drum;
        v.fade   = -1;
        v.age    = ++ageCounter;
        v.active = true;
    }

    void noteOffAudio (int, int) override {}   // one-shots ring out

    void allNotesOff() override { releasePending.store (true); }

    //==========================================================================
    static inline const juce::Identifier stateType { "Drums" };

    juce::ValueTree getState() const override
    {
        juce::ValueTree t (stateType);
        t.setProperty ("kit", kit.name, nullptr);
        t.setProperty ("drive", kit.drive, nullptr);
        t.setProperty ("dust", kit.dust, nullptr);
        for (int d = 0; d < Drums::NumDrums; ++d)
        {
            const auto& p = kit.drums[(size_t) d];
            juce::ValueTree c ("Drum");
            c.setProperty ("index", d, nullptr);
            c.setProperty ("tune", p.tune, nullptr);
            c.setProperty ("decay", p.decay, nullptr);
            c.setProperty ("tone", p.tone, nullptr);
            c.setProperty ("level", p.level, nullptr);
            t.appendChild (c, nullptr);
        }
        return t;
    }

    void setState (const juce::ValueTree& t) override
    {
        Drums::Kit k = Drums::factoryKits().front();
        if (auto* f = Drums::findFactoryKit (t["kit"].toString()))
            k = *f;
        k.name  = t.getProperty ("kit", k.name).toString();
        k.drive = t.getProperty ("drive", k.drive);
        k.dust  = t.getProperty ("dust", k.dust);
        for (int d = 0; d < Drums::NumDrums; ++d)
        {
            const auto c = t.getChildWithProperty ("index", d);
            if (! c.isValid()) continue;
            auto& p = k.drums[(size_t) d];
            p.tune  = c.getProperty ("tune", p.tune);
            p.decay = c.getProperty ("decay", p.decay);
            p.tone  = c.getProperty ("tone", p.tone);
            p.level = c.getProperty ("level", p.level);
        }
        loadKit (k);
    }

private:
    static constexpr double RenderRate = 48000.0;
    static constexpr float OutputGain = 0.55f;   // headroom for eight drums at once

    struct Rendered { std::array<std::vector<float>, NumTakes> takes; };

    struct Voice
    {
        const std::vector<float>* sample = nullptr;
        double pos = 0.0, step = 1.0;
        float gain = 0.0f;
        int drum = 0, fade = -1;
        juce::uint32 age = 0;
        bool active = false;
    };

    // Audio thread. Past the polyphony the oldest hit fades out (over ~3 ms,
    // in one of the spare slots) rather than cutting off with a click.
    Voice& freeVoice()
    {
        int ringing = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.active && v.fade < 0)
            {
                ++ringing;
                if (oldest == nullptr || v.age < oldest->age) oldest = &v;
            }
        if (ringing >= Polyphony && oldest != nullptr)
            oldest->fade = FadeSamples;

        Voice* best = nullptr;
        for (auto& v : voices)
        {
            if (! v.active) return v;
            if (best == nullptr || (v.fade >= 0 && (best->fade < 0 || v.fade < best->fade))) best = &v;
        }
        return *best;   // every slot busy: the one nearest the end of its fade
    }

    void renderDrum (int drum)
    {
        auto r = std::make_unique<Rendered>();
        for (int take = 0; take < NumTakes; ++take)
        {
            juce::Random rng (0xD5 + drum * 31 + take * 7919);   // same kit, same sound, every launch
            auto s = Drums::detail::render (drum, kit.drums[(size_t) drum], RenderRate, rng);
            Drums::detail::finish (s, kit, kit.drums[(size_t) drum].level, RenderRate);
            r->takes[(size_t) take] = std::move (s);
        }

        // Voices may still be playing the old one: keep it a while.
        if (auto* old = rendered[(size_t) drum].exchange (r.release()))
            retired.push_back ({ old, juce::Time::getMillisecondCounter() });
        freeRetired (false);
    }

    // Anything retired more than five seconds ago (no take is longer than
    // four) is no longer being played.
    void freeRetired (bool all)
    {
        const auto now = juce::Time::getMillisecondCounter();
        for (auto it = retired.begin(); it != retired.end();)
        {
            if (all || now - it->second > 5000) { delete it->first; it = retired.erase (it); }
            else ++it;
        }
    }

    void markAllDirty()
    {
        dirty.fill (true);
        if (! isTimerRunning()) startTimerHz (30);
    }

    void timerCallback() override
    {
        const bool anyDirty = std::any_of (dirty.begin(), dirty.end(), [] (bool b) { return b; });
        flush();
        if (! anyDirty && retired.empty())
            stopTimer();
    }

    // Message thread.
    Drums::Kit kit;
    std::array<bool, Drums::NumDrums> dirty {};
    std::vector<std::pair<Rendered*, juce::uint32>> retired;

    // Shared: the message thread swaps, the audio thread reads.
    std::array<std::atomic<Rendered*>, Drums::NumDrums> rendered {};

    // Audio thread.
    std::array<Voice, NumSlots> voices;
    std::array<int, Drums::NumDrums> nextTake {};
    double playbackStep = 1.0;
    juce::uint32 ageCounter = 0;
    std::atomic<bool> releasePending { false };
};
