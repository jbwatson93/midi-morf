#pragma once

#include <JuceHeader.h>
#include "MidiMorfColors.h"
#include "TrackRail.h"

//==============================================================================
// iPad: all four tracks' instruments at once, in a 2x2 grid. Each quarter is
// a slim header (track, what plays it, mute, expand) over the track's own
// instrument page, borrowed from the page container while Perform shows and
// handed back when it hides. Each instrument is on at most one track, so the
// same page never needs to be in two places.
//
// MainComponent fills it (populate) and takes the pages back (onReturnPage).
class PerformView : public juce::Component
{
public:
    PerformView()
    {
        for (int t = 0; t < TrackRack::NumTracks; ++t)
            addAndMakeVisible (quarters.add (new Quarter (*this, t)));
    }

    // MainComponent's side of the borrowing.
    std::function<void()> onShow;                            // populate now
    std::function<void (juce::Component*)> onReturnPage;     // give this page back
    std::function<void (int track)> onExpand;                // full screen for this track
    std::function<void (int track, bool muted)> onMute;
    std::function<void (int track)> onTouched;               // a finger landed in this quarter

    // page may be nullptr (no instrument): the quarter says so instead.
    void setQuarter (int track, juce::Component* page, const juce::String& text, bool muted)
    {
        if (auto* q = quarters[track])
            q->set (page, text, muted);
    }

    void returnAll()
    {
        for (auto* q : quarters)
            if (auto* p = q->release())
                if (onReturnPage) onReturnPage (p);
    }

    void visibilityChanged() override
    {
        if (isVisible()) { if (onShow) onShow(); }
        else             returnAll();
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (juce::Colour (0xff0E0F11));
    }

    void resized() override
    {
        auto r = getLocalBounds();
        const int gap = 6;
        const int w = (r.getWidth() - gap) / 2, h = (r.getHeight() - gap) / 2;
        for (int t = 0; t < quarters.size(); ++t)
            quarters[t]->setBounds (r.getX() + (t % 2) * (w + gap), r.getY() + (t / 2) * (h + gap), w, h);
    }

private:
    //==========================================================================
    struct Quarter : public juce::Component
    {
        Quarter (PerformView& o, int t) : owner (o), track (t)
        {
            mute.setClickingTogglesState (true);
            mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffB03A2E));
            mute.onClick = [this] { if (owner.onMute) owner.onMute (track, mute.getToggleState()); };
            addAndMakeVisible (mute);

            expand.onClick = [this] { if (owner.onExpand) owner.onExpand (track); };
            addAndMakeVisible (expand);
        }

        void set (juce::Component* newPage, const juce::String& text, bool muted)
        {
            if (newPage != page)
            {
                if (auto* old = release())
                    if (owner.onReturnPage) owner.onReturnPage (old);

                page = newPage;
                if (page != nullptr)
                {
                    addAndMakeVisible (page);
                    page->addMouseListener (this, true);   // to notice touches anywhere in it
                }
            }
            label = text;
            mute.setToggleState (muted, juce::dontSendNotification);
            resized();
            repaint();
        }

        juce::Component* release()
        {
            auto* p = std::exchange (page, nullptr);
            if (p != nullptr)
                p->removeMouseListener (this);
            return p;
        }

        void mouseDown (const juce::MouseEvent&) override
        {
            if (owner.onTouched) owner.onTouched (track);
        }

        void paint (juce::Graphics& g) override
        {
            const auto colour = TrackColours::get (track);
            auto r = getLocalBounds();
            auto head = r.removeFromTop (HeaderH).toFloat();

            g.setColour (juce::Colour (0xff17181B));
            g.fillRect (head);
            g.setColour (colour);
            g.fillRect (head.removeFromLeft (4.0f));
            g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            g.drawText (label, head.reduced (8.0f, 0.0f).withTrimmedRight (110.0f), juce::Justification::centredLeft);

            if (page == nullptr)
            {
                g.setColour (MidiMorfColors::textLight.withAlpha (0.45f));
                g.setFont (juce::FontOptions (14.0f));
                g.drawText ("Nothing plays this track. Set it up on Home.", r, juce::Justification::centred);
            }

            g.setColour (colour.withAlpha (0.7f));
            g.drawRect (getLocalBounds(), 1);
        }

        void resized() override
        {
            auto r = getLocalBounds();
            auto head = r.removeFromTop (HeaderH);
            expand.setBounds (head.removeFromRight (52).reduced (3));
            mute.setBounds (head.removeFromRight (56).reduced (3));
            if (page != nullptr)
                page->setBounds (r);
        }

        static constexpr int HeaderH = 28;

        PerformView& owner;
        const int track;
        juce::Component* page = nullptr;
        juce::String label;
        juce::TextButton mute { "MUTE" }, expand { "FULL" };
    };

    juce::OwnedArray<Quarter> quarters;
};
