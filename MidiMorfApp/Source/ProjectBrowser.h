#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "SynthLookAndFeel.h"

//==============================================================================
// Full-page project browser over the Session: the saved projects (newest
// first) and New / Save As / Duplicate / Delete. Tap a project to open it.
//
// Projects live in Documents/Projects as <name>.midimorf; Current.midimorf is
// the working copy and isn't listed.
class ProjectBrowser : public juce::Component
{
public:
    std::function<juce::String()> currentName;
    std::function<void (const juce::File&)> onOpen;   // {} for New
    std::function<void (const juce::String&)> onSaveAs;

    static juce::File folder()
    {
        return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Projects");
    }

    ProjectBrowser()
    {
        setLookAndFeel (&laf);
        title.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        title.setColour (juce::Label::textColourId, MidiMorfColors::textLight);
        addAndMakeVisible (title);

        newButton.onClick = [this] { confirmLeaving ("Start a new project?", [this] { if (onOpen) onOpen ({}); }); };
        saveAsButton.onClick = [this] { askName ("Save project as", isUntitled() ? juce::String() : name()); };
        duplicateButton.onClick = [this] { askName ("Duplicate as", name() + " copy"); };
        deleteButton.setClickingTogglesState (true);
        deleteButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffB03A2E));
        deleteButton.onClick = [this] { repaint(); };
        closeButton.onClick = [this] { setVisible (false); };
        for (auto* b : { &newButton, &saveAsButton, &duplicateButton, &deleteButton, &closeButton })
            addAndMakeVisible (b);
    }

    ~ProjectBrowser() override { setLookAndFeel (nullptr); }

    void show()
    {
        deleteButton.setToggleState (false, juce::dontSendNotification);
        rebuild();
        setVisible (true);
        toFront (true);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xf0121316));
        g.setColour (MidiMorfColors::accent.withAlpha (0.6f));
        g.drawRect (getLocalBounds(), 1);

        g.setColour (MidiMorfColors::textLight.withAlpha (0.5f));
        g.setFont (juce::FontOptions (13.0f));
        if (projectButtons.isEmpty())
            g.drawText ("No saved projects yet. Save As keeps this one.", gridArea, juce::Justification::centred);
        else if (deleteButton.getToggleState())
            g.drawText ("Tap a project to delete it.", hintArea, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        auto top = area.removeFromTop (36);
        closeButton.setBounds (top.removeFromRight (80).reduced (2));
        deleteButton.setBounds (top.removeFromRight (80).reduced (2));
        duplicateButton.setBounds (top.removeFromRight (96).reduced (2));
        saveAsButton.setBounds (top.removeFromRight (90).reduced (2));
        newButton.setBounds (top.removeFromRight (70).reduced (2));
        title.setBounds (top);
        hintArea = area.removeFromTop (22);
        gridArea = area;

        const int cols = 3, gap = 6, cellH = 44;
        const int cellW = (gridArea.getWidth() - (cols - 1) * gap) / cols;
        for (int i = 0; i < projectButtons.size(); ++i)
            projectButtons[i]->setBounds (gridArea.getX() + (i % cols) * (cellW + gap),
                                          gridArea.getY() + (i / cols) * (cellH + gap), cellW, cellH);
    }

private:
    juce::String name() const { return currentName ? currentName() : juce::String ("Untitled"); }
    bool isUntitled() const   { return name() == "Untitled"; }

    void rebuild()
    {
        title.setText (juce::String::fromUTF8 ("PROJECTS  \xc2\xb7  ") + name(), juce::dontSendNotification);

        auto files = folder().findChildFiles (juce::File::findFiles, false, "*.midimorf");
        files.removeIf ([] (const juce::File& f) { return f.getFileName() == "Current.midimorf"; });
        std::sort (files.begin(), files.end(), [] (const juce::File& a, const juce::File& b)
                   { return a.getLastModificationTime() > b.getLastModificationTime(); });

        projectButtons.clear();
        for (auto& f : files)
        {
            auto* b = projectButtons.add (new juce::TextButton (f.getFileNameWithoutExtension()));
            if (f.getFileNameWithoutExtension() == name())
                b->setColour (juce::TextButton::textColourOffId, MidiMorfColors::accent.brighter (0.3f));
            b->onClick = [this, f] { picked (f); };
            addAndMakeVisible (b);
        }
        resized();
        repaint();
    }

    void picked (const juce::File& f)
    {
        if (deleteButton.getToggleState())
        {
            const auto n = f.getFileNameWithoutExtension();
            juce::Component::SafePointer<ProjectBrowser> safe (this);
            juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle ("Delete " + n + "?")
                                              .withMessage ("This can't be undone.").withButton ("Delete").withButton ("Cancel"),
                [safe, f] (int r) { if (r == 1 && safe != nullptr) { f.deleteFile(); safe->rebuild(); } });
            return;
        }
        if (f.getFileNameWithoutExtension() == name())
        {
            setVisible (false);   // it's already open
            return;
        }
        confirmLeaving ("Open " + f.getFileNameWithoutExtension() + "?", [this, f] { if (onOpen) onOpen (f); });
    }

    // A named project is saved as you go; an untitled one would be lost.
    void confirmLeaving (const juce::String& question, std::function<void()> go)
    {
        if (! isUntitled())
        {
            setVisible (false);
            go();
            return;
        }
        juce::Component::SafePointer<ProjectBrowser> safe (this);
        juce::AlertWindow::showAsync (juce::MessageBoxOptions().withTitle (question)
                                          .withMessage ("This project isn't saved. Save As keeps it.")
                                          .withButton ("Continue").withButton ("Cancel"),
            [safe, go] (int r) { if (r == 1 && safe != nullptr) { safe->setVisible (false); go(); } });
    }

    void askName (const juce::String& heading, const juce::String& suggestion)
    {
        auto* w = new juce::AlertWindow (heading, {}, juce::MessageBoxIconType::NoIcon);
        w->addTextEditor ("name", suggestion);
        w->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        juce::Component::SafePointer<ProjectBrowser> safe (this);
        w->enterModalState (true, juce::ModalCallbackFunction::create ([safe, w] (int r)
        {
            const auto n = juce::File::createLegalFileName (w->getTextEditorContents ("name").trim());
            if (r != 1 || safe == nullptr || n.isEmpty() || n == "Current" || n == "Untitled")
                return;
            if (safe->onSaveAs) safe->onSaveAs (n);
            safe->rebuild();
        }), true);
    }

    SynthLookAndFeel laf;
    juce::Label title;
    juce::TextButton newButton { "New" }, saveAsButton { "Save As" }, duplicateButton { "Duplicate" },
                     deleteButton { "Delete" }, closeButton { "Close" };
    juce::OwnedArray<juce::TextButton> projectButtons;
    juce::Rectangle<int> gridArea, hintArea;
};
