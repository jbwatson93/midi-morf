#pragma once

#include <JuceHeader.h>

namespace MidiMorfColors
{
    // MPC-style light grey palette
    static const juce::Colour background      { 0xffC8C8C8 };  // Light grey background
    static const juce::Colour padBackground    { 0xff2A2A2E };  // Dark recessed pad area
    static const juce::Colour padBorder        { 0xff3A3A3E };  // Subtle pad border
    static const juce::Colour accent           { 0xffE85D3A };  // Orange-red accent (active states)
    static const juce::Colour buttonFace       { 0xff8A8A8A };  // Grey button face
    static const juce::Colour buttonFaceActive { 0xffE85D3A };  // Active button = accent
    static const juce::Colour buttonBorder     { 0xff6A6A6A };  // Button border
    static const juce::Colour textDark         { 0xff1A1A1A };  // Dark text on light backgrounds
    static const juce::Colour textLight        { 0xffE0E0E0 };  // Light text on dark backgrounds
    static const juce::Colour statusGreen      { 0xff4CAF50 };  // Connected
    static const juce::Colour statusAmber      { 0xffFF9800 };  // Searching
    static const juce::Colour crosshair        { 0x40FFFFFF };  // Translucent white crosshair
    static const juce::Colour dot              { 0xffE85D3A };  // Position dot = accent
}
