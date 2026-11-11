#pragma once

#include "Platform.h"

#include <JuceHeader.h>
#include "LocalSequencers.h"

//==============================================================================
// Importing audio, shared by Slice-Seq's BREAK picker and the Sampler page.
//
// iOS hands back a security-scoped URL, so the file is copied into the app's
// Documents/Breaks folder first. That copy is what gets remembered, so it
// still loads on the next launch.
namespace BreakImport
{
    // What the pickers offer. MP3, M4A/AAC and CAF are read by Core Audio.
    inline const juce::String patterns { Platform::importableAudioPatterns() };

    // The copy, or an empty File (with `error` set) if it couldn't be read.
    inline juce::File copyIn (const juce::URL& url, juce::String& error)
    {
        auto dir = Platform::documentsDirectory().getChildFile ("Breaks");
        dir.createDirectory();
        auto name = Platform::importedFileName (url);
        if (name.isEmpty()) name = "Imported.wav";
        auto dest = dir.getChildFile (name);

        if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
        {
            dest.deleteFile();
            juce::FileOutputStream out (dest);
            if (out.openedOk() && out.writeFromInputStream (*in, -1) > 0)
                return dest;
        }

        error = "Couldn't read " + name;
        return {};
    }

    // Pick a drum loop for the slicer. `chooser` must outlive the dialog —
    // callers keep it as a member. `onImported` runs only on success;
    // failures alert.
    inline void launch (std::unique_ptr<juce::FileChooser>& chooser, LocalSequencers& seqs, int track,
                        std::function<void()> onImported)
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a drum loop", juce::File(), patterns);
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [&seqs, track, onImported] (const juce::FileChooser& fc)
            {
                auto url = fc.getURLResult();
                if (url.isEmpty()) return;

                juce::String error;
                const auto dest = copyIn (url, error);
                if (dest != juce::File() && seqs.importBreak (track, dest, error))
                {
                    if (onImported) onImported();
                    return;
                }

                juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                                  .withIconType (juce::MessageBoxIconType::WarningIcon)
                                                  .withTitle ("Import failed")
                                                  .withMessage (error)
                                                  .withButton ("OK"),
                                              nullptr);
            });
    }
}
