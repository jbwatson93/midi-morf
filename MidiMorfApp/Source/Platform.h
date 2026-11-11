#pragma once

#include <JuceHeader.h>
#include <functional>

//==============================================================================
// Everything that differs between iOS and Android, so the shared code has no
// platform checks of its own. Each function is written once per platform, in
// Platform_ios.mm and Platform_android.cpp (AccelerometerManager lives there
// too). A function missing on one platform is a link error, not a silent gap.
namespace Platform
{
    // A big enough screen for the four-at-once Perform view.
    bool isTablet();

    // Whether the page must stay clear of the rail by itself, because the
    // screen's safe-area insets don't reliably cover it.
    bool needsRailGutter();

    // Asks for the microphone (the OS shows its prompt the first time) and
    // calls back on the message thread with the answer.
    void requestMicrophone (std::function<void (bool)> done);

    // Where to allow the mic after refusing it, for the error message.
    juce::String microphoneSettingsPath();

    // The app's own folder for Projects, Recordings, Presets and imports.
    juce::File documentsDirectory();

    // An imported file's name, with its extension, from what the file picker
    // returned. Empty if the picker doesn't say.
    juce::String importedFileName (const juce::URL& url);

    // The audio files this platform can decode, as a file chooser pattern.
    juce::String importableAudioPatterns();

    // The same, for people: "WAV, AIFF, ...".
    juce::String importableAudioNames();

    // Called once the audio device is open, to settle its rate and buffer.
    void configureAudioDevice (juce::AudioDeviceManager& deviceManager);

    // Whether to close the audio device when the app is backgrounded, rather
    // than only detaching the callback.
    bool closesAudioDeviceInBackground();

    // Settles how the app's window sits on screen, once at startup.
    void configureWindow();

    // Sets up whichever renderer draws the UI fastest here. Detach before the
    // component goes away.
    void attachRenderer (juce::Component& mainComponent);
    void detachRenderer();
}
