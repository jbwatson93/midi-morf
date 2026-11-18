#pragma once

#include <JuceHeader.h>
#include "WavetablePresets.h"

//==============================================================================
// Random patches for the wavetable synth. A roll starts from the presets'
// complete base patch and fills it in from a recipe for its category, with
// ranges taken from the factory presets, so a Bass roll sounds like a bass
// and not like noise. Mutate nudges the current patch a little instead.
//
// Both finish with matchLevel: the patch is played offline on a spare engine
// and its gain set so it peaks where the factory presets do. A roll is never
// silent or far louder than what was there before.
namespace PatchDice
{
    namespace detail
    {
        struct Dice
        {
            juce::Random& r;

            float range (float lo, float hi) const  { return lo + r.nextFloat() * (hi - lo); }
            // For Hz and seconds: even steps in octaves, not in Hz.
            float log (float lo, float hi) const    { return lo * std::pow (hi / lo, r.nextFloat()); }
            int   between (int lo, int hi) const    { return lo + r.nextInt (hi - lo + 1); }
            bool  chance (float p) const            { return r.nextFloat() < p; }

            template <typename T>
            T pick (std::initializer_list<T> options) const
            {
                return *(options.begin() + r.nextInt ((int) options.size()));
            }
        };

        inline juce::String adsr (float a, float d, float s, float rel)
        {
            return juce::String (a, 4) + "," + juce::String (d, 3) + "," + juce::String (s, 2) + "," + juce::String (rel, 3);
        }

        // Routes go in the first free matrix slot.
        inline void route (juce::ValueTree& t, int source, int dest, float amount)
        {
            for (int s = 0; s < WavetableEngine::NumRoutingSlots; ++s)
            {
                const juce::String n (s);
                if ((int) t["routeSrc_" + n] == 0 && (int) t["routeDest_" + n] == 0)
                {
                    t.setProperty ("routeSrc_" + n, source, nullptr);
                    t.setProperty ("routeDest_" + n, dest, nullptr);
                    t.setProperty ("routeAmount_" + n, amount, nullptr);
                    return;
                }
            }
        }

        inline void lfo (juce::ValueTree& t, int k, int shape, float rate, int mode = SynthMod::Free)
        {
            const juce::String p = "lfo" + juce::String (k) + "_";
            t.setProperty (p + "shape", SynthMod::toString (SynthMod::preset (shape)), nullptr);
            t.setProperty (p + "rate", rate, nullptr);
            t.setProperty (p + "mode", mode, nullptr);
        }

        // Tempo-synced, on the bar while the transport plays.
        inline void syncedLfo (juce::ValueTree& t, int k, int shape, int division, int mode = SynthMod::Free)
        {
            lfo (t, k, shape, 1.0f, mode);
            t.setProperty ("lfo" + juce::String (k) + "_sync", true, nullptr);
            t.setProperty ("lfo" + juce::String (k) + "_div", division, nullptr);
        }

        inline void set (juce::ValueTree& t, std::initializer_list<std::pair<const char*, juce::var>> props)
        {
            for (auto& [k, v] : props)
                t.setProperty (k, v, nullptr);
        }

        // Division indices into SynthMod::divisionBeats.
        constexpr int Div16 = 2, Div8 = 5, Div8Dot = 7, Div4 = 8, Div2 = 11, Bar = 12, TwoBars = 13;

        inline juce::String name (const Dice& d)
        {
            static const juce::StringArray first { "Rusty", "Velvet", "Neon", "Hollow", "Frozen", "Liquid", "Dusty", "Electric",
                                                   "Golden", "Broken", "Midnight", "Copper", "Feral", "Glass", "Static", "Lazy",
                                                   "Wired", "Smoky", "Cosmic", "Sour", "Bent", "Lunar", "Molten", "Paper" };
            static const juce::StringArray second { "Comet", "Engine", "Ghost", "Ribbon", "Signal", "Moth", "Harbor", "Circuit",
                                                    "Echo", "Tiger", "Prism", "Lantern", "Orbit", "Machine", "Garden", "Wave",
                                                    "Relic", "Swarm", "Bloom", "Static", "Drift", "Pulse", "Mirror", "Cloud" };
            return first[d.r.nextInt (first.size())] + " " + second[d.r.nextInt (second.size())];
        }
    }

    // One factory-style patch for `category` (one of WavetablePresets::
    // categories(); anything else picks one), with a made-up name.
    inline WavetablePreset roll (juce::String category, juce::Random& random)
    {
        using namespace detail;
        using E = WavetableEngine;
        const Dice d { random };

        const auto& cats = WavetablePresets::categories();
        if (! cats.contains (category))
            category = cats[random.nextInt (cats.size())];

        auto t = WavetablePresets::detail::base();
        set (t, { { "filtVel", d.range (0.3f, 0.8f) }, { "osc0_pos", d.range (0.0f, 1.0f) }, { "osc1_pos", d.range (0.0f, 1.0f) } });

        // Every factory preset answers the wheel and tilt; so does every roll.
        route (t, E::ModSrcModWheel, E::ModDestCutoff, d.range (0.1f, 0.4f));
        route (t, E::ModSrcAccelY, d.pick ({ (int) E::ModDestPosA, (int) E::ModDestCutoff }), d.range (0.2f, 0.5f));

        if (category == "Bass")
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "Saw", "Square", "Sine", "FM", "Fold", "Sync", "Digital", "Morph" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Saw", "Square", "Sine", "Triangle", "PWM" }) },
                      { "osc1_level", d.range (0.0f, 0.8f) }, { "osc1_semi", d.pick ({ -12, 0, 0, 12 }) }, { "osc1_fine", d.range (0.0f, 18.0f) },
                      { "unison", d.between (1, 2) }, { "detune", d.range (4.0f, 20.0f) },
                      { "sub", d.range (0.15f, 0.5f) }, { "drive", d.range (0.1f, 0.5f) },
                      { "filtType", d.pick ({ (int) SynthFilter::Ladder, (int) SynthFilter::LP24, (int) SynthFilter::LP12 }) },
                      { "cutoff", d.log (300.0f, 2000.0f) }, { "reso", d.range (0.2f, 2.4f) }, { "filtEnv", d.range (0.05f, 0.6f) },
                      { "amp", adsr (d.log (0.001f, 0.01f), d.range (0.2f, 0.5f), d.range (0.5f, 0.9f), d.log (0.05f, 0.2f)) },
                      { "fenv", adsr (d.log (0.001f, 0.01f), d.log (0.1f, 0.6f), d.range (0.0f, 0.4f), d.log (0.1f, 0.3f)) } });
            if (d.chance (0.5f))
                set (t, { { "voiceMode", d.pick ({ (int) E::Mono, (int) E::Legato }) }, { "glide", d.range (0.0f, 0.08f) } });
            if (d.chance (0.3f))   // wobble
            {
                syncedLfo (t, 1, d.pick ({ 0, 1, 7 }), d.pick ({ Div8, Div4, Div16 }));
                route (t, E::ModSrcLFO2, E::ModDestCutoff, d.range (0.3f, 0.5f));
            }
            if (d.chance (0.25f))  // a pluck of FM or bend at the front of the note
            {
                set (t, { { "osc0_warp", d.pick ({ (int) E::WarpFM, (int) E::WarpBendUp, (int) E::WarpSync }) },
                          { "osc0_warpAmt", d.range (0.1f, 0.3f) },
                          { "env3", adsr (0.001f, d.log (0.1f, 0.3f), 0.0f, 0.1f) } });
                route (t, E::ModSrcEnv3, E::ModDestWarpA, d.range (0.3f, 0.7f));
            }
            if (d.chance (0.3f))
                set (t, { { "distOn", true }, { "distDrive", d.range (0.1f, 0.35f) } });
        }
        else if (category == "Lead")
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "Saw", "Square", "Sync", "Vocal", "Digital", "Fold", "PWM", "Morph" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Saw", "Square", "PWM", "Sync", "Harmonics" }) },
                      { "osc1_level", d.range (0.2f, 0.8f) }, { "osc1_semi", d.pick ({ 0, 0, 12, -12, 7 }) }, { "osc1_fine", d.range (-9.0f, 9.0f) },
                      { "unison", d.between (1, 3) }, { "detune", d.range (5.0f, 15.0f) },
                      { "sub", d.chance (0.3f) ? d.range (0.1f, 0.25f) : 0.0f }, { "drive", d.range (0.0f, 0.3f) },
                      { "filtType", d.pick ({ (int) SynthFilter::Ladder, (int) SynthFilter::LP24, (int) SynthFilter::LP12 }) },
                      { "cutoff", d.log (2000.0f, 6000.0f) }, { "reso", d.range (0.4f, 1.4f) }, { "filtEnv", d.range (0.03f, 0.3f) },
                      { "amp", adsr (d.log (0.002f, 0.03f), d.range (0.2f, 0.4f), d.range (0.7f, 0.9f), d.range (0.15f, 0.35f)) },
                      { "fenv", adsr (0.005f, d.range (0.3f, 0.6f), d.range (0.2f, 0.5f), 0.3f) },
                      { "delayOn", d.chance (0.8f) }, { "delayTime", d.pick ({ 250.0, 333.0, 375.0, 500.0 }) },
                      { "delayFb", d.range (0.25f, 0.4f) }, { "delayMix", d.range (0.12f, 0.22f) },
                      { "revOn", true }, { "revMix", d.range (0.1f, 0.2f) } });
            if (d.chance (0.4f))
                set (t, { { "voiceMode", d.pick ({ (int) E::Mono, (int) E::Legato }) }, { "glide", d.range (0.05f, 0.15f) } });
            // Delayed vibrato, as a player would add it.
            lfo (t, 1, 0, d.range (4.5f, 6.5f), SynthMod::Retrigger);
            set (t, { { "lfo1_delay", d.range (0.2f, 0.5f) }, { "lfo1_rise", d.range (0.3f, 0.8f) } });
            route (t, E::ModSrcLFO2, E::ModDestPitch, d.range (0.01f, 0.025f));
            if (d.chance (0.3f))
            {
                set (t, { { "envPos", d.range (0.2f, 0.6f) } });
            }
        }
        else if (category == "Pad")
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "Morph", "Glass", "Vocal", "Harmonics", "Organ", "Saw", "PWM", "Digital" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Saw", "Sine", "Glass", "Vocal", "Triangle", "Morph" }) },
                      { "osc1_level", d.range (0.2f, 0.5f) }, { "osc1_semi", d.pick ({ 0, 12, 12, 7, -12 }) }, { "osc1_fine", d.range (5.0f, 12.0f) },
                      { "unison", d.between (4, 7) }, { "detune", d.range (8.0f, 20.0f) },
                      { "filtType", d.pick ({ (int) SynthFilter::Ladder, (int) SynthFilter::LP24, (int) SynthFilter::LP12 }) },
                      { "cutoff", d.log (1500.0f, 6000.0f) }, { "reso", d.range (0.2f, 1.0f) }, { "filtEnv", d.range (0.0f, 0.1f) },
                      { "amp", adsr (d.range (0.4f, 1.2f), d.range (1.0f, 2.0f), d.range (0.8f, 0.95f), d.range (1.5f, 3.0f)) },
                      { "fenv", adsr (d.range (0.8f, 1.5f), 2.0f, 0.6f, 2.0f) },
                      { "revOn", true }, { "revMix", d.range (0.35f, 0.5f) }, { "revSize", d.range (0.75f, 0.9f) },
                      { "delayOn", d.chance (0.4f) }, { "delayTime", d.pick ({ 375.0, 500.0, 750.0 }) },
                      { "delayFb", d.range (0.3f, 0.45f) }, { "delayMix", d.range (0.12f, 0.2f) } });
            // Slow movement through the table.
            lfo (t, 0, d.pick ({ 0, 1, 9 }), d.log (0.05f, 0.3f));
            route (t, E::ModSrcLFO, E::ModDestPosA, d.range (0.3f, 0.5f));
            if (d.chance (0.35f))   // a rhythmic gate, on the bar
            {
                syncedLfo (t, 2, d.pick ({ 7, 8 }), d.pick ({ Div8, Div16, Div8Dot }));
                route (t, E::ModSrcLFO3, E::ModDestGain, d.range (0.15f, 0.25f));
            }
        }
        else if (category == "Keys")
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "FM", "Organ", "Harmonics", "Glass", "Sine", "Triangle", "Digital" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Sine", "Triangle", "Organ", "Glass" }) },
                      { "osc1_level", d.range (0.1f, 0.5f) }, { "osc1_semi", d.pick ({ 0, 12, 12, 24 }) }, { "osc1_fine", d.range (0.0f, 5.0f) },
                      { "unison", d.between (1, 3) }, { "detune", d.range (3.0f, 10.0f) },
                      { "filtType", d.pick ({ (int) SynthFilter::LP24, (int) SynthFilter::LP12 }) },
                      { "cutoff", d.log (2000.0f, 5000.0f) }, { "reso", d.range (0.2f, 0.8f) }, { "filtEnv", d.range (0.1f, 0.3f) },
                      { "filtKey", d.range (0.2f, 0.6f) },
                      { "amp", adsr (d.log (0.002f, 0.01f), d.range (0.6f, 1.5f), d.range (0.3f, 0.6f), d.range (0.3f, 0.6f)) },
                      { "fenv", adsr (0.002f, d.range (0.3f, 0.8f), 0.2f, 0.4f) },
                      { "revOn", true }, { "revMix", d.range (0.15f, 0.3f) } });
            if (d.chance (0.4f))   // tremolo
            {
                lfo (t, 1, 0, d.range (3.0f, 6.0f));
                route (t, E::ModSrcLFO2, E::ModDestGain, d.range (0.08f, 0.15f));
            }
        }
        else if (category == "Pluck")
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "Saw", "Square", "Digital", "Harmonics", "Glass", "FM", "PWM", "Fold" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Saw", "Square", "Triangle", "Glass" }) },
                      { "osc1_level", d.range (0.1f, 0.6f) }, { "osc1_semi", d.pick ({ 0, 12, -12, 7 }) }, { "osc1_fine", d.range (0.0f, 8.0f) },
                      { "unison", d.between (1, 3) }, { "detune", d.range (5.0f, 14.0f) },
                      { "filtType", d.pick ({ (int) SynthFilter::Ladder, (int) SynthFilter::LP24, (int) SynthFilter::LP12 }) },
                      { "cutoff", d.log (800.0f, 3000.0f) }, { "reso", d.range (0.3f, 1.6f) }, { "filtEnv", d.range (0.3f, 0.6f) },
                      { "filtKey", d.range (0.2f, 0.5f) },
                      { "amp", adsr (d.log (0.001f, 0.003f), d.range (0.2f, 0.6f), d.range (0.0f, 0.1f), d.range (0.2f, 0.4f)) },
                      { "fenv", adsr (0.001f, d.log (0.1f, 0.35f), 0.0f, d.range (0.1f, 0.3f)) },
                      { "delayOn", d.chance (0.7f) }, { "delayTime", d.pick ({ 250.0, 333.0, 375.0 }) },
                      { "delayFb", d.range (0.3f, 0.45f) }, { "delayMix", d.range (0.18f, 0.3f) },
                      { "revOn", true }, { "revMix", d.range (0.15f, 0.3f) } });
        }
        else   // FX: looser, with more going on
        {
            set (t, { { "osc0_tableName", d.pick<const char*> ({ "Digital", "Fold", "FM", "Sync", "Vocal", "Glass", "Harmonics", "Morph" }) },
                      { "osc1_tableName", d.pick<const char*> ({ "Digital", "Fold", "FM", "Square", "Saw", "PWM" }) },
                      { "osc1_level", d.range (0.0f, 0.8f) }, { "osc1_semi", d.pick ({ -12, 0, 7, 12, 19 }) }, { "osc1_fine", d.range (-25.0f, 25.0f) },
                      { "unison", d.between (1, 5) }, { "detune", d.range (5.0f, 30.0f) },
                      { "noise", d.chance (0.3f) ? d.range (0.05f, 0.2f) : 0.0f }, { "drive", d.range (0.0f, 0.5f) },
                      { "filtType", d.between (0, SynthFilter::NumTypes - 1) },
                      { "cutoff", d.log (400.0f, 6000.0f) }, { "reso", d.range (0.3f, 2.4f) }, { "filtEnv", d.range (-0.3f, 0.5f) },
                      { "amp", adsr (d.log (0.002f, 1.0f), d.range (0.3f, 1.5f), d.range (0.4f, 0.9f), d.range (0.3f, 1.5f)) },
                      { "fenv", adsr (d.log (0.002f, 1.0f), d.range (0.3f, 1.5f), d.range (0.0f, 0.6f), 0.5f) },
                      { "osc0_warp", d.chance (0.5f) ? d.between (1, E::NumWarps - 1) : 0 }, { "osc0_warpAmt", d.range (0.1f, 0.5f) },
                      { "distOn", d.chance (0.5f) }, { "distDrive", d.range (0.2f, 0.6f) },
                      { "delayOn", d.chance (0.7f) }, { "delayTime", d.pick ({ 166.0, 250.0, 375.0, 500.0 }) },
                      { "delayFb", d.range (0.3f, 0.6f) }, { "delayMix", d.range (0.15f, 0.3f) },
                      { "revOn", d.chance (0.7f) }, { "revMix", d.range (0.2f, 0.45f) }, { "revSize", d.range (0.5f, 0.9f) } });

            syncedLfo (t, 1, d.between (0, 10), d.pick ({ Div16, Div8, Div4, Bar }), d.pick ({ (int) SynthMod::Free, (int) SynthMod::Retrigger }));
            lfo (t, 2, d.between (0, 10), d.log (0.1f, 8.0f));
            const int sources[] = { E::ModSrcLFO2, E::ModSrcLFO3, E::ModSrcRandom, E::ModSrcEnv2 };
            const int dests[]   = { E::ModDestPosA, E::ModDestPosB, E::ModDestCutoff, E::ModDestWarpA, E::ModDestPitchB,
                                    E::ModDestResonance, E::ModDestDelayMix, E::ModDestPan };
            for (int n = d.between (2, 4); --n >= 0;)
            {
                const int dest = dests[random.nextInt ((int) std::size (dests))];
                const float depth = dest == E::ModDestPitchB ? d.range (0.05f, 0.3f) : d.range (0.2f, 0.6f);
                route (t, sources[random.nextInt ((int) std::size (sources))], dest, d.chance (0.25f) ? -depth : depth);
            }
        }

        return { name (d), category, t, {} };
    }

    // The patch, nudged: every knob moves by up to `amount` of its range, and
    // now and then a table, an LFO shape or a route changes outright. The FX
    // rack is left alone.
    inline juce::ValueTree mutate (const juce::ValueTree& current, juce::Random& random, float amount = 0.12f)
    {
        using E = WavetableEngine;
        const detail::Dice d { random };
        auto t = current.createCopy();

        auto nudge = [&] (const juce::String& key, float lo, float hi)
        {
            if (! t.hasProperty (key)) return;
            const float v = (float) (double) t[key];
            t.setProperty (key, juce::jlimit (lo, hi, v + d.range (-amount, amount) * (hi - lo)), nullptr);
        };
        // Octave steps for Hz, so a nudge sounds the same size anywhere.
        auto nudgeLog = [&] (const juce::String& key, float lo, float hi)
        {
            if (! t.hasProperty (key)) return;
            const float v = juce::jlimit (lo, hi, (float) (double) t[key]);
            t.setProperty (key, juce::jlimit (lo, hi, v * std::pow (hi / lo, d.range (-amount, amount))), nullptr);
        };
        auto nudgeEnv = [&] (const juce::String& key)
        {
            if (! t.hasProperty (key)) return;
            auto parts = juce::StringArray::fromTokens (t[key].toString(), ",", {});
            if (parts.size() != 4) return;
            float v[4];
            for (int k = 0; k < 4; ++k) v[k] = parts[k].getFloatValue();
            for (int k : { 0, 1, 3 })
                v[k] = juce::jlimit (0.001f, 8.0f, v[k] * std::pow (2.0f, d.range (-amount, amount) * 4.0f));
            v[2] = juce::jlimit (0.0f, 1.0f, v[2] + d.range (-amount, amount));
            t.setProperty (key, detail::adsr (v[0], v[1], v[2], v[3]), nullptr);
        };

        for (const juce::String o : { "osc0_", "osc1_" })
        {
            nudge (o + "pos", 0.0f, 1.0f);
            nudge (o + "fine", -50.0f, 50.0f);
            nudge (o + "warpAmt", 0.0f, 1.0f);
            if (d.chance (amount * 0.8f))
            {
                const auto& names = WavetableLibrary::builtInNames();
                if (WavetableLibrary::indexForName (t[o + "tableName"].toString()) >= 0)
                    t.setProperty (o + "tableName", names[random.nextInt (names.size())], nullptr);
            }
        }
        nudge ("osc1_level", 0.0f, 1.0f);
        nudge ("detune", 0.0f, 40.0f);
        nudge ("sub", 0.0f, 0.6f);
        nudge ("drive", 0.0f, 0.6f);
        nudgeLog ("cutoff", 200.0f, 12000.0f);
        nudge ("reso", 0.0f, 2.6f);
        nudge ("filtEnv", -0.6f, 0.8f);
        nudge ("envPos", -1.0f, 1.0f);
        nudgeEnv ("amp");
        nudgeEnv ("fenv");
        nudgeEnv ("env3");

        for (int k = 0; k < E::NumLfos; ++k)
        {
            const juce::String p = "lfo" + juce::String (k) + "_";
            if (! t.hasProperty (p + "shape")) continue;
            if (! (bool) t[p + "sync"]) nudgeLog (p + "rate", 0.03f, 12.0f);
            if (d.chance (amount * 0.5f))
                t.setProperty (p + "shape", SynthMod::toString (SynthMod::preset (random.nextInt (SynthMod::presetNames().size()))), nullptr);
        }

        for (int s = 0; s < E::NumRoutingSlots; ++s)
        {
            const juce::String n (s);
            if ((int) t["routeSrc_" + n] == 0) continue;
            const float a = (float) (double) t["routeAmount_" + n];
            t.setProperty ("routeAmount_" + n, juce::jlimit (-1.0f, 1.0f, a * (1.0f + d.range (-amount, amount) * 2.0f)), nullptr);
        }
        return t;
    }

    //==========================================================================
    // Plays a four-note chord on `scratch` (a spare engine, never the one a
    // track plays) and sets the patch's gain so the held chord peaks where
    // the factory presets do. It's held through the attack and decay, so a
    // slow pad is measured at full level. Gain goes in before the drive, so
    // it takes a second pass to settle.
    inline void matchLevel (juce::ValueTree& t, WavetableEngine& scratch)
    {
        constexpr double rate = 48000.0;
        constexpr int block = 256, notes[] = { 48, 55, 60, 64 };
        constexpr float targetPeak = 0.5f;

        juce::AudioBuffer<float> buf (2, block);
        scratch.prepare (rate, block);
        scratch.setTempo (120.0);

        const auto amp = juce::StringArray::fromTokens (t["amp"].toString(), ",", {});
        const double hold = juce::jlimit (0.5, 3.0, amp.size() == 4 ? amp[0].getDoubleValue() + amp[1].getDoubleValue() : 1.0);

        for (int pass = 0; pass < 2; ++pass)
        {
            scratch.setState (t);
            float peak = 0.0f;
            for (int b = 0; b < (int) (rate * hold) / block; ++b)
            {
                if (b == 0) for (int n : notes) scratch.noteOnAudio (1, n, 100);
                buf.clear();
                scratch.render (buf, 0, block);
                for (int c = 0; c < 2; ++c)
                    peak = juce::jmax (peak, buf.getMagnitude (c, 0, block));
            }
            scratch.allNotesOff();

            if (! std::isfinite (peak) || peak < 1.0e-4f)
                break;   // nothing to measure: leave the gain as rolled
            const float gain = (float) (double) t.getProperty ("gain", 0.15);
            t.setProperty ("gain", juce::jlimit (0.01f, 0.4f, gain * targetPeak / peak), nullptr);
        }
    }
}
