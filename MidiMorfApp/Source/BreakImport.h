#pragma once

#include <JuceHeader.h>
#include "LocalSequencers.h"

//==============================================================================
// "Import .wav..." for drum loops, shared by Slice-Seq's BREAK picker and the
// Breaks sound page.
//
// iOS hands back a security-scoped URL, so the loop is copied into the app's
// Documents/Breaks folder first. That copy is what gets remembered, so it
// still loads on the next launch. `chooser` must outlive the dialog — callers
// keep it as a member. `onImported` runs only on success; failures alert.
namespace BreakImport
{
    inline void launch (std::unique_ptr<juce::FileChooser>& chooser, LocalSequencers& seqs,
                        std::function<void()> onImported)
    {
        chooser = std::make_unique<juce::FileChooser> ("Import a drum loop", juce::File(), "*.wav;*.aif;*.aiff");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
            [&seqs, onImported] (const juce::FileChooser& fc)
            {
                auto url = fc.getURLResult();
                if (url.isEmpty()) return;

                auto dir = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                               .getChildFile ("Breaks");
                dir.createDirectory();
                auto name = url.getFileName().isNotEmpty() ? url.getFileName() : juce::String ("Imported.wav");
                auto dest = dir.getChildFile (juce::URL::removeEscapeChars (name));

                bool copied = false;
                if (auto in = url.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)))
                {
                    dest.deleteFile();
                    juce::FileOutputStream out (dest);
                    copied = out.openedOk() && out.writeFromInputStream (*in, -1) > 0;
                }

                juce::String error;
                if (! copied)
                    error = "Couldn't read " + name;
                else if (seqs.importBreak (dest, error))
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
