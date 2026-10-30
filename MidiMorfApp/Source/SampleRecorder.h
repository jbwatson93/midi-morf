#pragma once

#include <JuceHeader.h>
#include <atomic>

//==============================================================================
// Records the mic into a sample. A second callback on the audio device,
// alongside the rack: it plays nothing and keeps the first input channel
// while recording. The buffer is sized when the device starts, so the audio
// thread never allocates.
//
// AudioEngine attaches it only while the mic is on (opening the input asks
// iOS for the microphone and switches the session to play-and-record).
class SampleRecorder : public juce::AudioIODeviceCallback
{
public:
    static constexpr double MaxSeconds = 30.0;

    //==========================================================================
    // Message thread.
    void start()
    {
        written.store (0);
        recording.store (true);
    }

    bool isRecording() const noexcept { return recording.load(); }
    double getSampleRate() const noexcept { return rate.load(); }
    double getSecondsRecorded() const noexcept { return written.load() / rate.load(); }

    // Recent input peak, 0..1. Read for the meter; reading clears it.
    float takeLevel() noexcept { return level.exchange (0.0f); }

    // Stops and hands back what was recorded, trimmed and levelled — or an
    // empty buffer if nothing above the noise floor came in (no sound, or
    // the mic is blocked in Settings).
    juce::AudioBuffer<float> stop()
    {
        recording.store (false);
        const juce::SpinLock::ScopedLockType sl (bufferLock);   // the callback may still be mid-copy
        return finish (buffer, written.load(), rate.load());
    }

    // Cut the silence at either end, fade the edges so the cuts don't click,
    // and bring the peak to the same level as rendered presets.
    static juce::AudioBuffer<float> finish (const juce::AudioBuffer<float>& source, int length, double sampleRate)
    {
        length = juce::jmin (length, source.getNumSamples());
        if (length <= 0)
            return {};

        const float* s = source.getReadPointer (0);
        const auto range = juce::FloatVectorOperations::findMinAndMax (s, length);
        const float peak = juce::jmax (range.getEnd(), -range.getStart());
        if (peak < 0.003f)   // about -50 dBFS: nothing came in
            return {};

        // The ends are where the signal first and last rises above 5% of its
        // peak, with a little room either side for the attack and the tail.
        const float threshold = peak * 0.05f;
        int first = 0, last = length - 1;
        while (first < length && std::abs (s[first]) < threshold) ++first;
        while (last > first && std::abs (s[last]) < threshold) --last;
        first = juce::jmax (0, first - (int) (sampleRate * 0.01));
        last  = juce::jmin (length - 1, last + (int) (sampleRate * 0.15));

        const int n = last - first + 1;
        juce::AudioBuffer<float> out (1, n);
        out.copyFrom (0, 0, source, 0, first, n);
        out.applyGain (0.7f / peak);

        const int fadeIn = juce::jmin (n / 2, (int) (sampleRate * 0.002)), fadeOut = juce::jmin (n / 2, (int) (sampleRate * 0.02));
        out.applyGainRamp (0, fadeIn, 0.0f, 1.0f);
        out.applyGainRamp (n - fadeOut, fadeOut, 1.0f, 0.0f);
        return out;
    }

    //==========================================================================
    // AudioIODeviceCallback — audio thread.
    void audioDeviceAboutToStart (juce::AudioIODevice* device) override
    {
        const juce::SpinLock::ScopedLockType sl (bufferLock);
        const double r = device->getCurrentSampleRate() > 0.0 ? device->getCurrentSampleRate() : 48000.0;
        recording.store (false);
        written.store (0);
        buffer.setSize (1, (int) (r * MaxSeconds));
        rate.store (r);
    }

    void audioDeviceStopped() override { recording.store (false); }

    void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs, float* const* outputs, int numOutputs,
                                           int numSamples, const juce::AudioIODeviceCallbackContext&) override
    {
        for (int ch = 0; ch < numOutputs; ++ch)
            if (outputs[ch] != nullptr)
                juce::FloatVectorOperations::clear (outputs[ch], numSamples);

        if (numInputs < 1 || inputs[0] == nullptr)
            return;

        const auto range = juce::FloatVectorOperations::findMinAndMax (inputs[0], numSamples);
        const float peak = juce::jmax (range.getEnd(), -range.getStart());
        if (peak > level.load())
            level.store (peak);

        if (! recording.load())
            return;

        const juce::SpinLock::ScopedTryLockType sl (bufferLock);
        if (! sl.isLocked())
            return;

        const int at = written.load();
        const int n = juce::jmin (numSamples, buffer.getNumSamples() - at);
        if (n > 0)
            buffer.copyFrom (0, at, inputs[0], n);
        written.store (at + juce::jmax (0, n));
        if (n < numSamples)
            recording.store (false);   // full: the page notices and finishes
    }

private:
    juce::AudioBuffer<float> buffer;
    juce::SpinLock bufferLock;
    std::atomic<int> written { 0 };
    std::atomic<double> rate { 48000.0 };
    std::atomic<bool> recording { false };
    std::atomic<float> level { 0.0f };
};
