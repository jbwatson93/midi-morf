#pragma once

#include <JuceHeader.h>
#include "PopupStyle.h"
#include "XYPad.h"
#include "FXButton.h"
#include "AxisSlider.h"
#include "EventRouter.h"
#include "MidiMorfColors.h"
#include "MidiMorfProtocol.h"
#include "Glassmorphism.h"

// Deep teal palette — studio rack effects vibe. Lives at file scope so
// FXButton instances can hold a pointer to it.
static const PagePalette fxPagePalette {
    juce::Colour (0xff0F2A33),       // substrate top — deep teal
    juce::Colour (0xff061418),       // substrate bottom — near-black with teal undertone
    juce::Colour (0xCC1F4550),       // acrylic top — translucent indigo-teal
    juce::Colour (0xD8112832),       // acrylic bottom — deeper translucent
    juce::Colour (0xCC000000)        // edge — translucent black
};

// Morf FX. In WiFi mode the XY pad, sliders and four buttons send CCs and
// button messages to the DAW. In Local mode they play the punch-in FX on the
// master bus: hold a button to punch its effect in, double-tap to latch it,
// tap a latched one to let it go. The chevron on each button picks its
// effect, and the sliders say what X and Y do for the effect last touched.
class FXPunchInPage : public juce::Component,
                      public XYPad::Listener,
                      public FXButton::Listener,
                      public AxisSlider::Listener
{
public:
    FXPunchInPage (EventRouter& client)
        : networkClient (client),
          ySlider (false, "Y", 21),
          xSlider (true,  "X", 20)
    {
        xyPad.setListener (this);
        addAndMakeVisible (xyPad);

        ySlider.setListener (this);
        addAndMakeVisible (ySlider);

        xSlider.setListener (this);
        addAndMakeVisible (xSlider);

        for (int i = 0; i < MidiMorf::NUM_SLOTS; ++i)
        {
            auto* btn = new FXButton (i);
            btn->setListener (this);
            btn->setPalette (&fxPagePalette);
            fxButtons.add (btn);
            addAndMakeVisible (btn);
            wifiLabels.add ("FX " + juce::String (i + 1));
        }
    }

    // A slot name from the server, for WiFi mode.
    void setButtonLabel (int slotIndex, const juce::String& name)
    {
        if (slotIndex >= 0 && slotIndex < fxButtons.size())
        {
            wifiLabels.set (slotIndex, name);
            refreshMode();
        }
    }

    std::function<void()> onStateChanged;   // an effect was picked — worth saving

    // Which effect sits on each button.
    void saveState (juce::PropertySet& props) const
    {
        if (auto* punch = networkClient.getPunchFx())
            for (int i = 0; i < PunchFx::NumSlots; ++i)
                props.setValue ("punch_slot_" + juce::String (i), punch->getSlotEffect (i));
    }

    void restoreState (const juce::PropertySet& props)
    {
        if (auto* punch = networkClient.getPunchFx())
            for (int i = 0; i < PunchFx::NumSlots; ++i)
            {
                const juce::String key = "punch_slot_" + juce::String (i);
                if (props.containsKey (key))
                    punch->setSlotEffect (i, props.getIntValue (key));
            }
        refreshMode();
    }

    // Call when the WiFi / Local mode changes.
    void refreshFromRouter() { refreshMode(); }

    // Labels follow the mode — the page may have been hidden while it changed.
    void visibilityChanged() override
    {
        if (isVisible())
            refreshMode();
        else
            releaseLocal();   // leaving the page lets go of anything latched
    }

    void paint (juce::Graphics& g) override
    {
        // FX page paints its own deep-teal glass panel over the PageContainer's
        // theme background — gives FX Punch-in a distinct identity from the
        // chord/instrument pages.
        Glassmorphism::paintPanel (g, getLocalBounds().toFloat(), fxPagePalette);
    }

    void resized() override
    {
        // Landscape layout:
        //   • XY pad fills the left, with the Y slider on its left edge
        //   • 2×2 FX button grid on the right
        //   • X slider runs along the bottom under the XY pad (full width up
        //     to the FX block)
        auto area = getLocalBounds().reduced (12);

        // Right column: FX buttons (2×2)
        const int fxColW = juce::jmax (140, area.getWidth() / 4);
        auto fxArea = area.removeFromRight (fxColW);

        int rowH = (fxArea.getHeight() - 6) / 2;
        int colW = (fxArea.getWidth()  - 6) / 2;
        fxButtons[0]->setBounds (fxArea.getX(),              fxArea.getY(),              colW, rowH);
        fxButtons[1]->setBounds (fxArea.getX() + colW + 6,   fxArea.getY(),              colW, rowH);
        fxButtons[2]->setBounds (fxArea.getX(),              fxArea.getY() + rowH + 6,   colW, rowH);
        fxButtons[3]->setBounds (fxArea.getX() + colW + 6,   fxArea.getY() + rowH + 6,   colW, rowH);

        area.removeFromRight (8);

        // X slider along the bottom of the remaining left region
        auto xSliderArea = area.removeFromBottom (36);
        xSlider.setBounds (xSliderArea);
        area.removeFromBottom (6);

        // Y slider on the left edge of the XY pad
        auto ySliderArea = area.removeFromLeft (44);
        ySlider.setBounds (ySliderArea);
        area.removeFromLeft (6);

        // XY pad fills the rest
        xyPad.setBounds (area);
    }

    // XYPad::Listener
    void xyPadUpdated (float x, float y) override
    {
        networkClient.sendXYUpdate (x, y);
        xSlider.setDisplayValue (x);
        ySlider.setDisplayValue (y);
    }

    // FXButton::Listener
    void fxButtonPressed (int slotIndex) override
    {
        lastPressedSlot = slotIndex;
        updateHighlights();

        if (! local())
        {
            networkClient.sendButtonDown (slotIndex);
            return;
        }

        auto& s = slotTouch[(size_t) slotIndex];
        s.downAt = juce::Time::getMillisecondCounter();

        // A tap on a latched effect lets it go.
        if (fxButtons[slotIndex]->isLatched())
        {
            fxButtons[slotIndex]->setLatched (false);
            networkClient.sendButtonUp (slotIndex);
            s.swallowRelease = true;
            return;
        }

        networkClient.sendButtonDown (slotIndex);
    }

    void fxButtonReleased (int slotIndex) override
    {
        if (! local())
        {
            networkClient.sendButtonUp (slotIndex);
            moveHighlightFrom (slotIndex);
            return;
        }

        auto& s = slotTouch[(size_t) slotIndex];
        if (std::exchange (s.swallowRelease, false))
            return;

        // Two quick taps latch: the second release keeps the effect in.
        const auto now = juce::Time::getMillisecondCounter();
        const bool quickTap = now - s.downAt < 250;
        if (quickTap && now - s.lastTapUp < 400)
        {
            fxButtons[slotIndex]->setLatched (true);
            s.lastTapUp = 0;
            return;
        }

        s.lastTapUp = quickTap ? now : 0;
        networkClient.sendButtonUp (slotIndex);
        moveHighlightFrom (slotIndex);
    }

    void fxButtonMenu (int slotIndex) override
    {
        auto* punch = networkClient.getPunchFx();
        if (punch == nullptr || ! local())
            return;

        juce::PopupMenu menu;
        const auto& names = PunchFx::effectNames();
        for (int e = 0; e < names.size(); ++e)
            menu.addItem (e + 1, names[e], true, punch->getSlotEffect (slotIndex) == e);

        juce::Component::SafePointer<FXPunchInPage> safe (this);
        menu.showMenuAsync (PopupStyle::attached (menu, *fxButtons[slotIndex], 36),
            [safe, slotIndex] (int result)
            {
                if (safe == nullptr || result <= 0) return;
                if (auto* p = safe->networkClient.getPunchFx())
                    p->setSlotEffect (slotIndex, result - 1);
                safe->lastPressedSlot = slotIndex;
                safe->refreshMode();
                if (safe->onStateChanged) safe->onStateChanged();
            });
    }

    // AxisSlider::Listener
    void axisSliderUpdated (bool isXAxis, float value) override
    {
        networkClient.sendAxisUpdate (isXAxis, value);
    }

private:
    bool local() const { return networkClient.isLocal() && networkClient.getPunchFx() != nullptr; }

    void refreshMode()
    {
        auto* punch = networkClient.getPunchFx();
        const bool isLocal = local();

        for (int i = 0; i < fxButtons.size(); ++i)
        {
            fxButtons[i]->setShowsMenu (isLocal);
            fxButtons[i]->setLabel (isLocal ? PunchFx::effectNames()[punch->getSlotEffect (i)] : wifiLabels[i]);
            if (! isLocal)
                fxButtons[i]->setLatched (false);
        }

        if (isLocal)
        {
            const auto axes = PunchFx::axisNames (punch->getSlotEffect (juce::jmax (0, lastPressedSlot)));
            xSlider.setLocalLabel ("X  " + axes.first);
            ySlider.setLocalLabel ("Y");
        }
        else
        {
            xSlider.setLocalLabel ({});
            ySlider.setLocalLabel ({});
        }
        updateHighlights();
    }

    void releaseLocal()
    {
        if (! local())
            return;
        for (int i = 0; i < fxButtons.size(); ++i)
            if (fxButtons[i]->isLatched())
            {
                fxButtons[i]->setLatched (false);
                networkClient.sendButtonUp (i);
            }
    }

    // If the released slot was the last pressed, the highlight moves to
    // another held one.
    void moveHighlightFrom (int slotIndex)
    {
        if (slotIndex == lastPressedSlot)
            for (int i = 0; i < fxButtons.size(); ++i)
                if (i != slotIndex && (fxButtons[i]->isHeld() || fxButtons[i]->isLatched()))
                {
                    lastPressedSlot = i;
                    break;
                }
        updateHighlights();
    }

    void updateHighlights()
    {
        for (int i = 0; i < fxButtons.size(); ++i)
            fxButtons[i]->setHighlighted (i == lastPressedSlot);

        if (local())
        {
            const auto axes = PunchFx::axisNames (networkClient.getPunchFx()->getSlotEffect (juce::jmax (0, lastPressedSlot)));
            xSlider.setLocalLabel ("X  " + axes.first);
            ySlider.setLocalLabel (axes.second);
        }
    }

    struct Touch { juce::uint32 downAt = 0, lastTapUp = 0; bool swallowRelease = false; };

    EventRouter& networkClient;
    XYPad xyPad;
    AxisSlider ySlider;
    AxisSlider xSlider;
    juce::OwnedArray<FXButton> fxButtons;
    juce::StringArray wifiLabels;
    std::array<Touch, MidiMorf::NUM_SLOTS> slotTouch;
    int lastPressedSlot = -1;
};
