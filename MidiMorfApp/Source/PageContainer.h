#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"

class PageContainer : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();

        // 1. Theme substrate — sits behind the translucent acrylic so the
        //    panel reads cleanly regardless of what's behind.
        {
            juce::ColourGradient base (
                MidiMorfColors::panelSubstrateTop,    0.0f, 0.0f,
                MidiMorfColors::panelSubstrateBottom, 0.0f, bounds.getHeight(),
                false);
            g.setGradientFill (base);
            g.fillAll();
        }

        // 2. Translucent themed acrylic body — same look as the synth page so
        //    every controller shares the panel aesthetic.
        juce::ColourGradient plastic (
            MidiMorfColors::panelAcrylicTop,    0.0f, 0.0f,
            MidiMorfColors::panelAcrylicBottom, 0.0f, bounds.getHeight(),
            false);
        g.setGradientFill (plastic);
        g.fillAll();

        // 3. Glassy top sheen — light catching the top of a curved plastic
        //    surface.
        auto sheen = bounds.withHeight (bounds.getHeight() * 0.22f);
        juce::ColourGradient sg (juce::Colour (0x55FFFFFF), sheen.getX(), sheen.getY(),
                                  juce::Colour (0x00FFFFFF), sheen.getX(), sheen.getBottom(),
                                  false);
        g.setGradientFill (sg);
        g.fillRect (sheen);

        // 4. Subtle bottom shadow — adds depth.
        auto shadow = bounds.withTrimmedTop (bounds.getHeight() * 0.65f);
        juce::ColourGradient sh (juce::Colour (0x00000000), shadow.getX(), shadow.getY(),
                                  juce::Colour (0x44000000), shadow.getX(), shadow.getBottom(),
                                  false);
        g.setGradientFill (sh);
        g.fillRect (shadow);

        // 5. Soft themed edge glow — finishes the acrylic vibe.
        g.setColour (MidiMorfColors::panelEdgeGlow);
        g.drawRoundedRectangle (bounds.reduced (1.0f), 8.0f, 1.5f);
    }

    struct Listener
    {
        virtual ~Listener() = default;
        virtual void pageChanged (int newPageIndex) = 0;
    };

    PageContainer() {}

    void setListener (Listener* l) { listener = l; }

    void addPage (juce::Component* page)
    {
        pages.add (page);
        addChildComponent (page);

        if (pages.size() == 1)
        {
            page->setVisible (true);
            currentPage = 0;
        }
    }

    int getCurrentPage() const { return currentPage; }

    // Takes back a page another view borrowed (the iPad Perform view shows
    // several at once): hidden again, at full size, unless it's current.
    void reclaim (juce::Component* page)
    {
        if (page == nullptr || ! pages.contains (page))
            return;
        addChildComponent (page);
        page->setBounds (getLocalBounds());
        page->setVisible (pages.indexOf (page) == currentPage);
    }
    int getNumPages() const { return pages.size(); }
    juce::Component* getPage (int index) const { return pages[index]; }

    void setPage (int index)
    {
        if (index < 0 || index >= pages.size() || index == currentPage)
            return;

        pages[currentPage]->setVisible (false);
        currentPage = index;
        pages[currentPage]->setVisible (true);
        pages[currentPage]->setBounds (getLocalBounds());

        if (listener != nullptr)
            listener->pageChanged (currentPage);
    }

    void resized() override
    {
        // Pages lent to the Perform view are its to lay out (it sizes them to
        // their quarter); only the ones still here are full size.
        auto area = getLocalBounds();
        for (int i = 0; i < pages.size(); ++i)
            if (pages[i]->getParentComponent() == this)
                pages[i]->setBounds (area);
    }

private:
    juce::Array<juce::Component*> pages;
    int currentPage = 0;
    Listener* listener = nullptr;
};
