#pragma once

#include <JuceHeader.h>
#include "WavetableEngine.h"
#include <vector>

//==============================================================================
// Presets for the wavetable synth. A preset is a whole patch — the same
// ValueTree WavetableEngine::getState gives — plus a name and a category.
//
// Factory presets are built here and read-only. User presets are files,
// Documents/Presets/Wavetable/<Category>/<Name>.mmpreset, so they show in the
// Files app and can be shared.
struct WavetablePreset
{
    juce::String name, category;
    juce::ValueTree state;
    juce::File file;   // empty for factory presets

    bool isFactory() const { return file == juce::File(); }
};

namespace WavetablePresets
{
    inline const juce::StringArray& categories()
    {
        static const juce::StringArray c { "Bass", "Lead", "Pad", "Keys", "Pluck", "FX" };
        return c;
    }

    //==========================================================================
    namespace detail
    {
        using Props = std::initializer_list<std::pair<const char*, juce::var>>;
        using Route = std::tuple<int, int, float>;   // source, destination, amount

        // A complete patch: every property the engine stores, so loading a
        // preset never inherits anything from the patch before it. These are
        // the engine's own defaults, with no routings and dry FX — each
        // factory preset says what it uses.
        inline juce::ValueTree base()
        {
            juce::ValueTree t (WavetableEngine::stateType);
            t.setProperty ("version", 1, nullptr);
            const Props props {
                { "osc0_tableName", "Morph" }, { "osc0_file", "" }, { "osc0_level", 1.0 }, { "osc0_pos", 0.6 }, { "osc0_semi", 0 }, { "osc0_fine", 0.0 },
                { "osc1_tableName", "PWM" },   { "osc1_file", "" }, { "osc1_level", 0.0 }, { "osc1_pos", 0.0 }, { "osc1_semi", 0 }, { "osc1_fine", 7.0 },
                { "unison", 3 }, { "detune", 12.0 }, { "sub", 0.0 }, { "drive", 0.0 }, { "gain", 0.15 },
                { "cutoff", 2500.0 }, { "reso", 0.8 }, { "filtEnv", 2000.0 }, { "envPos", 0.0 },
                { "amp", "0.01,0.4,0.7,0.6" }, { "fenv", "0.01,0.5,0.3,0.5" },
                { "lfoRate", 0.3 }, { "lfoDepth", 0.0 }, { "lfoWave", 0 },
                { "distDrive", 0.0 }, { "distOn", false },
                { "delayTime", 350.0 }, { "delayFb", 0.4 }, { "delayMix", 0.15 }, { "delayOn", false },
                { "revMix", 0.25 }, { "revSize", 0.6 }, { "revOn", false },
            };
            for (auto& [k, v] : props)
                t.setProperty (k, v, nullptr);
            for (int i = 0; i < WavetableEngine::NumRoutingSlots; ++i)
            {
                t.setProperty ("routeSrc_" + juce::String (i), 0, nullptr);
                t.setProperty ("routeDest_" + juce::String (i), 0, nullptr);
                t.setProperty ("routeAmount_" + juce::String (i), 0.0, nullptr);
            }
            return t;
        }

        inline WavetablePreset make (const char* name, const char* category, Props props, std::initializer_list<Route> routes = {})
        {
            auto t = base();
            for (auto& [k, v] : props)
                t.setProperty (k, v, nullptr);
            int slot = 0;
            for (auto& [src, dest, amount] : routes)
            {
                const juce::String n (slot++);
                t.setProperty ("routeSrc_" + n, src, nullptr);
                t.setProperty ("routeDest_" + n, dest, nullptr);
                t.setProperty ("routeAmount_" + n, amount, nullptr);
            }
            return { name, category, t, {} };
        }
    }

    //==========================================================================
    // Init: exactly what the engine starts with (WavetableEngine::
    // resetToDefaults), tilt scanning the table and the mod wheel on cutoff.
    inline WavetablePreset init()
    {
        using E = WavetableEngine;
        return detail::make ("Init", "", { { "distOn", true }, { "delayOn", true }, { "revOn", true } },
                             { { E::ModSrcAccelY, E::ModDestPosA, 0.5f }, { E::ModSrcModWheel, E::ModDestCutoff, 0.5f } });
    }

    // First drafts, to be tuned by ear on the phone. Levels are matched to
    // within a few dB (~-20 dBFS RMS for two held notes) so stepping through
    // them doesn't jump in volume.
    inline const std::vector<WavetablePreset>& factory()
    {
        using E = WavetableEngine;
        using detail::make;
        constexpr int MW = E::ModSrcModWheel, Vel = E::ModSrcVelocity, AX = E::ModSrcAccelX, AY = E::ModSrcAccelY, LFO = E::ModSrcLFO;
        constexpr int PosA = E::ModDestPosA, PosB = E::ModDestPosB, Cut = E::ModDestCutoff, Res = E::ModDestResonance,
                      Pitch = E::ModDestPitch, Gain = E::ModDestGain;

        static const std::vector<WavetablePreset> all {
            // ── Bass ────────────────────────────────────────────────────────
            make ("Acid Bass", "Bass",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Square" }, { "osc1_level", 0.45 }, { "osc1_semi", -12 }, { "osc1_fine", 4.0 },
                    { "unison", 1 }, { "sub", 0.35 }, { "drive", 0.35 }, { "gain", 0.36 },
                    { "cutoff", 420.0 }, { "reso", 2.4 }, { "filtEnv", 3200.0 },
                    { "amp", "0.002,0.3,0.6,0.09" }, { "fenv", "0.001,0.16,0.0,0.1" } },
                  { { Vel, Cut, 0.35f }, { MW, Res, 0.3f } }),
            make ("Sub Bass", "Bass",
                  { { "osc0_tableName", "Sine" }, { "osc1_tableName", "Triangle" }, { "osc1_level", 0.3 }, { "osc1_semi", 12 }, { "osc1_fine", 0.0 },
                    { "unison", 1 }, { "sub", 0.5 }, { "drive", 0.15 }, { "gain", 0.18 },
                    { "cutoff", 900.0 }, { "reso", 0.2 }, { "filtEnv", 300.0 },
                    { "amp", "0.005,0.2,0.9,0.15" }, { "fenv", "0.005,0.3,0.2,0.2" } },
                  { { MW, Cut, 0.3f } }),
            make ("Reese", "Bass",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.9 }, { "osc1_fine", 18.0 },
                    { "unison", 2 }, { "detune", 22.0 }, { "sub", 0.2 }, { "drive", 0.4 }, { "gain", 0.22 },
                    { "cutoff", 650.0 }, { "reso", 1.2 }, { "filtEnv", 900.0 },
                    { "amp", "0.01,0.5,0.85,0.2" }, { "fenv", "0.01,0.6,0.4,0.3" },
                    { "lfoRate", 0.25 }, { "lfoDepth", 0.5 }, { "lfoWave", 1 } },
                  { { LFO, Cut, 0.25f }, { MW, Cut, 0.5f } }),
            make ("Wobble", "Bass",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Square" }, { "osc1_level", 0.6 }, { "osc1_semi", -12 },
                    { "unison", 2 }, { "detune", 10.0 }, { "sub", 0.3 }, { "drive", 0.5 }, { "gain", 0.17 },
                    { "cutoff", 300.0 }, { "reso", 2.6 }, { "filtEnv", 600.0 },
                    { "amp", "0.005,0.3,0.9,0.12" }, { "lfoRate", 2.0 }, { "lfoDepth", 1.0 }, { "lfoWave", 0 },
                    { "distOn", true }, { "distDrive", 0.2 } },
                  { { LFO, Cut, 0.8f }, { MW, Cut, 0.3f } }),
            make ("FM Bass", "Bass",
                  { { "osc0_tableName", "FM" }, { "osc0_pos", 0.3 }, { "osc1_tableName", "Sine" }, { "osc1_level", 0.5 }, { "osc1_semi", -12 }, { "osc1_fine", 0.0 },
                    { "unison", 1 }, { "sub", 0.2 }, { "drive", 0.3 }, { "gain", 0.28 },
                    { "cutoff", 1800.0 }, { "reso", 0.6 }, { "filtEnv", 2500.0 }, { "envPos", 0.4 },
                    { "amp", "0.002,0.35,0.5,0.1" }, { "fenv", "0.001,0.25,0.1,0.1" } },
                  { { Vel, Cut, 0.4f }, { AY, PosA, 0.3f } }),

            // ── Lead ────────────────────────────────────────────────────────
            make ("Saw Lead", "Lead",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.8 }, { "osc1_fine", 9.0 },
                    { "unison", 3 }, { "detune", 14.0 }, { "drive", 0.2 }, { "gain", 0.16 },
                    { "cutoff", 3200.0 }, { "reso", 1.0 }, { "filtEnv", 2500.0 },
                    { "amp", "0.005,0.3,0.8,0.25" }, { "fenv", "0.005,0.4,0.4,0.3" },
                    { "lfoRate", 5.5 }, { "lfoDepth", 1.0 },
                    { "delayOn", true }, { "delayTime", 375.0 }, { "delayFb", 0.35 }, { "delayMix", 0.2 },
                    { "revOn", true }, { "revMix", 0.15 } },
                  { { LFO, Pitch, 0.015f }, { MW, Cut, 0.5f } }),
            make ("Sync Lead", "Lead",
                  { { "osc0_tableName", "Sync" }, { "osc0_pos", 0.3 }, { "osc1_tableName", "Square" }, { "osc1_level", 0.3 }, { "osc1_semi", 12 },
                    { "unison", 1 }, { "drive", 0.3 }, { "gain", 0.18 },
                    { "cutoff", 5000.0 }, { "reso", 0.7 }, { "filtEnv", 1500.0 }, { "envPos", 0.6 },
                    { "amp", "0.005,0.2,0.8,0.2" }, { "fenv", "0.005,0.5,0.2,0.3" },
                    { "delayOn", true }, { "delayTime", 250.0 }, { "delayFb", 0.3 }, { "delayMix", 0.18 },
                    { "revOn", true }, { "revMix", 0.12 } },
                  { { AY, PosA, 0.6f }, { MW, PosA, 0.4f } }),
            make ("Vocal Lead", "Lead",
                  { { "osc0_tableName", "Vocal" }, { "osc0_pos", 0.2 }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.3 }, { "osc1_fine", 6.0 },
                    { "unison", 2 }, { "detune", 8.0 }, { "gain", 0.26 },
                    { "cutoff", 4000.0 }, { "reso", 0.5 }, { "filtEnv", 800.0 },
                    { "amp", "0.02,0.3,0.85,0.3" }, { "lfoRate", 0.5 }, { "lfoDepth", 0.6 }, { "lfoWave", 1 },
                    { "delayOn", true }, { "delayMix", 0.15 }, { "revOn", true }, { "revMix", 0.3 }, { "revSize", 0.7 } },
                  { { LFO, PosA, 0.5f }, { AY, PosA, 0.3f } }),
            make ("Square Lead", "Lead",
                  { { "osc0_tableName", "Square" }, { "osc1_tableName", "PWM" }, { "osc1_level", 0.6 }, { "osc1_pos", 0.5 }, { "osc1_fine", -5.0 },
                    { "unison", 1 }, { "sub", 0.25 }, { "gain", 0.17 },
                    { "cutoff", 2600.0 }, { "reso", 1.4 }, { "filtEnv", 1800.0 },
                    { "amp", "0.003,0.25,0.75,0.18" }, { "fenv", "0.003,0.3,0.3,0.2" },
                    { "lfoRate", 0.4 }, { "lfoDepth", 0.7 },
                    { "delayOn", true }, { "delayTime", 333.0 }, { "delayFb", 0.3 }, { "delayMix", 0.15 },
                    { "revOn", true }, { "revMix", 0.12 } },
                  { { LFO, PosB, 0.6f }, { MW, Cut, 0.4f } }),

            // ── Pad ─────────────────────────────────────────────────────────
            make ("Warm Pad", "Pad",
                  { { "osc0_tableName", "Morph" }, { "osc0_pos", 0.55 }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.4 }, { "osc1_fine", 9.0 },
                    { "unison", 5 }, { "detune", 18.0 }, { "gain", 0.15 },
                    { "cutoff", 1800.0 }, { "reso", 0.4 }, { "filtEnv", 600.0 },
                    { "amp", "0.9,1.5,0.85,2.2" }, { "fenv", "1.2,2.0,0.6,2.0" },
                    { "lfoRate", 0.15 }, { "lfoDepth", 0.4 },
                    { "revOn", true }, { "revMix", 0.45 }, { "revSize", 0.85 } },
                  { { LFO, PosA, 0.4f }, { AY, PosA, 0.4f }, { MW, Cut, 0.5f } }),
            make ("Glass Pad", "Pad",
                  { { "osc0_tableName", "Glass" }, { "osc0_pos", 0.4 }, { "osc1_tableName", "Sine" }, { "osc1_level", 0.3 }, { "osc1_semi", 12 },
                    { "unison", 4 }, { "detune", 10.0 }, { "gain", 0.13 },
                    { "cutoff", 6000.0 }, { "reso", 0.3 }, { "filtEnv", 0.0 },
                    { "amp", "0.6,1.0,0.9,2.5" }, { "lfoRate", 0.2 }, { "lfoDepth", 0.5 },
                    { "delayOn", true }, { "delayTime", 500.0 }, { "delayFb", 0.45 }, { "delayMix", 0.2 },
                    { "revOn", true }, { "revMix", 0.5 }, { "revSize", 0.9 } },
                  { { LFO, PosA, 0.5f }, { AY, PosA, 0.3f } }),
            make ("Choir", "Pad",
                  { { "osc0_tableName", "Vocal" }, { "osc0_pos", 0.5 }, { "osc1_tableName", "Vocal" }, { "osc1_level", 0.6 }, { "osc1_pos", 0.8 }, { "osc1_fine", 8.0 },
                    { "unison", 5 }, { "detune", 16.0 }, { "gain", 0.16 },
                    { "cutoff", 3000.0 }, { "reso", 0.3 }, { "filtEnv", 0.0 },
                    { "amp", "0.7,1.0,0.9,1.8" }, { "lfoRate", 0.1 }, { "lfoDepth", 0.6 }, { "lfoWave", 1 },
                    { "revOn", true }, { "revMix", 0.5 }, { "revSize", 0.85 } },
                  { { LFO, PosA, 0.6f }, { AX, PosB, 0.4f } }),
            make ("Evolving", "Pad",
                  { { "osc0_tableName", "Harmonics" }, { "osc1_tableName", "Morph" }, { "osc1_level", 0.6 },
                    { "unison", 4 }, { "detune", 14.0 }, { "gain", 0.12 },
                    { "cutoff", 2200.0 }, { "reso", 0.9 }, { "filtEnv", 1500.0 },
                    { "amp", "1.5,2.0,0.8,3.0" }, { "fenv", "2.5,3.0,0.3,3.0" },
                    { "lfoRate", 0.08 }, { "lfoDepth", 1.0 },
                    { "delayOn", true }, { "delayTime", 600.0 }, { "delayFb", 0.5 }, { "delayMix", 0.2 },
                    { "revOn", true }, { "revMix", 0.55 }, { "revSize", 0.9 } },
                  { { LFO, PosA, 0.8f }, { AY, Cut, 0.4f } }),
            make ("Dark Pad", "Pad",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Triangle" }, { "osc1_level", 0.6 }, { "osc1_semi", -12 },
                    { "unison", 5 }, { "detune", 20.0 }, { "sub", 0.2 }, { "gain", 0.17 },
                    { "cutoff", 700.0 }, { "reso", 0.8 }, { "filtEnv", 400.0 },
                    { "amp", "1.2,1.5,0.9,2.5" }, { "revOn", true }, { "revMix", 0.5 }, { "revSize", 0.9 } },
                  { { MW, Cut, 0.7f }, { AY, Cut, 0.3f } }),

            // ── Keys ────────────────────────────────────────────────────────
            make ("FM Keys", "Keys",
                  { { "osc0_tableName", "FM" }, { "osc0_pos", 0.2 }, { "osc1_tableName", "Sine" }, { "osc1_level", 0.4 }, { "osc1_semi", 12 }, { "osc1_fine", 0.0 },
                    { "unison", 1 }, { "gain", 0.14 },
                    { "cutoff", 5000.0 }, { "reso", 0.2 }, { "filtEnv", 2000.0 }, { "envPos", 0.5 },
                    { "amp", "0.002,1.2,0.3,0.6" }, { "fenv", "0.002,0.8,0.1,0.5" },
                    { "revOn", true }, { "revMix", 0.25 } },
                  { { Vel, PosA, 0.4f }, { Vel, Gain, 0.3f } }),
            make ("Organ", "Keys",
                  { { "osc0_tableName", "Organ" }, { "osc0_pos", 0.5 }, { "osc1_tableName", "Organ" }, { "osc1_level", 0.5 }, { "osc1_pos", 0.2 }, { "osc1_semi", 12 }, { "osc1_fine", 0.0 },
                    { "unison", 1 }, { "gain", 0.13 },
                    { "cutoff", 8000.0 }, { "reso", 0.1 }, { "filtEnv", 0.0 },
                    { "amp", "0.005,0.1,1.0,0.08" }, { "lfoRate", 6.0 }, { "lfoDepth", 0.3 },
                    { "revOn", true }, { "revMix", 0.2 } },
                  { { LFO, Gain, 0.2f }, { MW, PosA, 0.5f } }),
            make ("Bell Keys", "Keys",
                  { { "osc0_tableName", "Glass" }, { "osc0_pos", 0.7 }, { "osc1_tableName", "FM" }, { "osc1_level", 0.4 }, { "osc1_pos", 0.5 }, { "osc1_semi", 12 }, { "osc1_fine", 0.0 },
                    { "unison", 2 }, { "detune", 6.0 }, { "gain", 0.22 },
                    { "cutoff", 7000.0 }, { "filtEnv", 3000.0 }, { "envPos", 0.4 },
                    { "amp", "0.001,2.0,0.0,1.5" }, { "fenv", "0.001,1.0,0.0,1.0" },
                    { "delayOn", true }, { "delayTime", 400.0 }, { "delayFb", 0.3 }, { "delayMix", 0.15 },
                    { "revOn", true }, { "revMix", 0.35 }, { "revSize", 0.75 } }),
            make ("Soft Keys", "Keys",
                  { { "osc0_tableName", "Triangle" }, { "osc1_tableName", "Sine" }, { "osc1_level", 0.5 }, { "osc1_semi", 12 }, { "osc1_fine", 0.0 },
                    { "unison", 2 }, { "detune", 5.0 }, { "gain", 0.15 },
                    { "cutoff", 2400.0 }, { "reso", 0.3 }, { "filtEnv", 1500.0 },
                    { "amp", "0.005,0.9,0.4,0.5" }, { "fenv", "0.005,0.6,0.2,0.4" },
                    { "revOn", true }, { "revMix", 0.25 } },
                  { { Vel, Cut, 0.5f } }),

            // ── Pluck ───────────────────────────────────────────────────────
            make ("Pluck", "Pluck",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Square" }, { "osc1_level", 0.4 },
                    { "unison", 2 }, { "detune", 8.0 }, { "gain", 0.3 },
                    { "cutoff", 600.0 }, { "reso", 1.2 }, { "filtEnv", 5000.0 },
                    { "amp", "0.001,0.35,0.0,0.3" }, { "fenv", "0.001,0.18,0.0,0.2" },
                    { "delayOn", true }, { "delayTime", 375.0 }, { "delayFb", 0.35 }, { "delayMix", 0.25 },
                    { "revOn", true }, { "revMix", 0.2 } },
                  { { Vel, Cut, 0.4f } }),
            make ("Harp", "Pluck",
                  { { "osc0_tableName", "Triangle" }, { "osc1_tableName", "Glass" }, { "osc1_level", 0.5 }, { "osc1_pos", 0.3 }, { "osc1_fine", 0.0 },
                    { "unison", 1 }, { "gain", 0.16 },
                    { "cutoff", 3000.0 }, { "reso", 0.3 }, { "filtEnv", 3000.0 },
                    { "amp", "0.001,1.4,0.0,1.2" }, { "fenv", "0.001,0.5,0.0,0.5" },
                    { "revOn", true }, { "revMix", 0.35 }, { "revSize", 0.7 } }),
            make ("Digital Pluck", "Pluck",
                  { { "osc0_tableName", "Digital" }, { "osc0_pos", 0.4 }, { "osc1_tableName", "Square" }, { "osc1_level", 0.3 }, { "osc1_semi", 12 },
                    { "unison", 1 }, { "drive", 0.3 }, { "gain", 0.28 },
                    { "cutoff", 1500.0 }, { "reso", 1.5 }, { "filtEnv", 4000.0 }, { "envPos", 0.7 },
                    { "amp", "0.001,0.25,0.0,0.2" }, { "fenv", "0.001,0.15,0.0,0.15" },
                    { "delayOn", true }, { "delayTime", 250.0 }, { "delayFb", 0.4 }, { "delayMix", 0.25 } },
                  { { AY, PosA, 0.5f } }),
            make ("Muted Pluck", "Pluck",
                  { { "osc0_tableName", "Square" }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.3 },
                    { "unison", 1 }, { "gain", 0.26 },
                    { "cutoff", 400.0 }, { "reso", 0.8 }, { "filtEnv", 1500.0 },
                    { "amp", "0.001,0.15,0.0,0.1" }, { "fenv", "0.001,0.08,0.0,0.1" },
                    { "revOn", true }, { "revMix", 0.1 } }),

            // ── FX ──────────────────────────────────────────────────────────
            make ("Tilt Sweep", "FX",
                  { { "osc0_tableName", "Morph" }, { "osc1_tableName", "Fold" }, { "osc1_level", 0.6 },
                    { "unison", 3 }, { "detune", 15.0 }, { "gain", 0.2 },
                    { "cutoff", 1200.0 }, { "reso", 2.0 }, { "filtEnv", 0.0 },
                    { "amp", "0.05,0.5,0.9,0.8" },
                    { "delayOn", true }, { "delayTime", 375.0 }, { "delayFb", 0.5 }, { "delayMix", 0.25 },
                    { "revOn", true }, { "revMix", 0.4 }, { "revSize", 0.8 } },
                  { { AY, PosA, 1.0f }, { AX, Cut, 0.8f }, { AY, PosB, 0.8f } }),
            make ("Riser", "FX",
                  { { "osc0_tableName", "Saw" }, { "osc1_tableName", "Saw" }, { "osc1_level", 0.8 }, { "osc1_fine", 25.0 },
                    { "unison", 5 }, { "detune", 35.0 }, { "gain", 0.12 },
                    { "cutoff", 3000.0 }, { "reso", 2.5 }, { "filtEnv", 0.0 },
                    { "amp", "2.0,1.0,1.0,1.5" }, { "lfoRate", 0.1 }, { "lfoDepth", 1.0 }, { "lfoWave", 2 },
                    { "revOn", true }, { "revMix", 0.5 }, { "revSize", 0.9 } },
                  { { LFO, Cut, 0.5f }, { LFO, Pitch, 0.3f } }),
            make ("Bit Grit", "FX",
                  { { "osc0_tableName", "Digital" }, { "osc0_pos", 0.8 }, { "osc1_tableName", "Fold" }, { "osc1_level", 0.5 },
                    { "unison", 1 }, { "drive", 0.9 }, { "gain", 0.1 },
                    { "cutoff", 3000.0 }, { "reso", 1.8 }, { "filtEnv", 2000.0 },
                    { "amp", "0.002,0.4,0.6,0.3" }, { "lfoRate", 8.0 }, { "lfoDepth", 0.5 }, { "lfoWave", 3 },
                    { "distOn", true }, { "distDrive", 0.6 },
                    { "delayOn", true }, { "delayTime", 166.0 }, { "delayFb", 0.55 }, { "delayMix", 0.3 } },
                  { { LFO, PosA, 0.5f } }),
        };
        return all;
    }

    inline const WavetablePreset* findFactory (const juce::String& name)
    {
        for (auto& p : factory())
            if (p.name == name)
                return &p;
        return nullptr;
    }

    //==========================================================================
    // User presets.
    inline juce::File userFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                   .getChildFile ("Presets").getChildFile ("Wavetable");
    }

    inline std::vector<WavetablePreset> user()
    {
        std::vector<WavetablePreset> out;
        for (auto& f : userFolder().findChildFiles (juce::File::findFiles, true, "*.mmpreset"))
        {
            auto xml = juce::XmlDocument::parse (f);
            if (xml == nullptr || ! xml->hasTagName ("MidiMorfPreset"))
                continue;
            auto tree = juce::ValueTree::fromXml (*xml);
            auto state = tree.getChildWithName (WavetableEngine::stateType);
            if (! state.isValid())
                continue;
            out.push_back ({ tree["name"].toString(), tree["category"].toString(), state, f });
        }
        std::sort (out.begin(), out.end(), [] (const WavetablePreset& a, const WavetablePreset& b)
                   { return a.name.compareNatural (b.name) < 0; });
        return out;
    }

    // Overwrites a user preset of the same name and category.
    inline bool save (const juce::String& name, const juce::String& category, const juce::ValueTree& state, juce::String& error)
    {
        const auto clean = juce::File::createLegalFileName (name.trim());
        if (clean.isEmpty())
        {
            error = "Give the preset a name.";
            return false;
        }

        juce::ValueTree tree ("MidiMorfPreset");
        tree.setProperty ("name", name.trim(), nullptr);
        tree.setProperty ("category", category, nullptr);
        tree.setProperty ("type", "Wavetable", nullptr);
        tree.appendChild (state.createCopy(), nullptr);

        const auto dir = userFolder().getChildFile (juce::File::createLegalFileName (category));
        if (! dir.createDirectory())
        {
            error = "Couldn't create " + dir.getFullPathName();
            return false;
        }

        const auto file = dir.getChildFile (clean + ".mmpreset");
        auto xml = tree.createXml();
        if (xml == nullptr || ! xml->writeTo (file))
        {
            error = "Couldn't write " + file.getFileName();
            return false;
        }
        return true;
    }

    // Every preset in browsing order: Init, then each category's factory
    // presets followed by the user's.
    inline std::vector<WavetablePreset> all()
    {
        std::vector<WavetablePreset> out { init() };
        const auto mine = user();
        for (auto& cat : categories())
        {
            for (auto& p : factory()) if (p.category == cat) out.push_back (p);
            for (auto& p : mine)      if (p.category == cat) out.push_back (p);
        }
        for (auto& p : mine)
            if (! categories().contains (p.category))
                out.push_back (p);
        return out;
    }
}
