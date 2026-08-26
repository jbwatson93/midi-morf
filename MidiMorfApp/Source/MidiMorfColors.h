#pragma once

#include <JuceHeader.h>

//==============================================================================
// Build-time theme switch. Change MIDIMORF_THEME below, rebuild (~30s), and the
// whole app re-skins. Once a favourite is picked the runtime theme picker on
// the About page will replace this with an enum + listener pattern — but a
// #define is the fastest way to evaluate all three on real hardware against
// the wood frame and decide.
//
//   0 = Original Purple   — the deep purple glassmorphism + orange accent
//                            JB designed first; the look we shipped on. Default.
//   1 = Studio Bronze     — warm vintage, harmonises with the wood frame
//   2 = Modern Producer   — cool dark, mint accent, Logic-Pro adjacent
//   3 = Anodized Slate    — refined coral-orange on slate, industrial
//   4 = Light Grey Console — MPC-style brushed light grey panel, dark pads sit
//                            on top like hardware; orange accent stays
//==============================================================================
#ifndef MIDIMORF_THEME
 #define MIDIMORF_THEME 4
#endif

namespace MidiMorfColors
{
   #if MIDIMORF_THEME == 0
    //========================== Original Purple ===============================
    static const juce::Colour background        { 0xffC8C8C8 };
    static const juce::Colour padBackground     { 0xff2A2A2E };
    static const juce::Colour padBorder         { 0xff3A3A3E };
    static const juce::Colour accent            { 0xffE85D3A };   // orange-red
    static const juce::Colour buttonFace        { 0xff8A8A8A };
    static const juce::Colour buttonFaceActive  { 0xffE85D3A };
    static const juce::Colour buttonBorder      { 0xff6A6A6A };
    static const juce::Colour textDark          { 0xff1A1A1A };
    static const juce::Colour textLight         { 0xffE0E0E0 };
    static const juce::Colour statusGreen       { 0xff4CAF50 };
    static const juce::Colour statusAmber       { 0xffFF9800 };
    static const juce::Colour crosshair         { 0x40FFFFFF };
    static const juce::Colour dot               { 0xffE85D3A };

    static const juce::Colour panelSubstrateTop    { 0xff2D1458 };
    static const juce::Colour panelSubstrateBottom { 0xff160726 };
    static const juce::Colour panelAcrylicTop      { 0xCC5A28B0 };
    static const juce::Colour panelAcrylicBottom   { 0xD83218A0 };
    static const juce::Colour panelEdgeGlow        { 0x88B080FF };

   #elif MIDIMORF_THEME == 1
    //========================== Studio Bronze =================================
    static const juce::Colour background        { 0xff1F1A14 };
    static const juce::Colour padBackground     { 0xff2A2520 };
    static const juce::Colour padBorder         { 0xff3A332A };
    static const juce::Colour accent            { 0xffD4A85C };   // brass
    static const juce::Colour buttonFace        { 0xff3D362E };
    static const juce::Colour buttonFaceActive  { 0xffD4A85C };
    static const juce::Colour buttonBorder      { 0xff5A4F42 };
    static const juce::Colour textDark          { 0xff1A1410 };
    static const juce::Colour textLight         { 0xffF2EBDA };   // warm cream
    static const juce::Colour statusGreen       { 0xff7BAB6B };
    static const juce::Colour statusAmber       { 0xffE8B05C };
    static const juce::Colour crosshair         { 0x40FFFFFF };
    static const juce::Colour dot               { 0xffD4A85C };

    // Glassmorphism panel — 5-layer stack used by PageContainer,
    // SynthSettingsPage, and the Rose-Chord button LookAndFeel.
    // Sheen / shadow are hardcoded white/black overlays at the call sites.
    static const juce::Colour panelSubstrateTop    { 0xff2A1F14 };
    static const juce::Colour panelSubstrateBottom { 0xff140C06 };
    static const juce::Colour panelAcrylicTop      { 0xCC4A3520 };
    static const juce::Colour panelAcrylicBottom   { 0xD82D1F0F };
    static const juce::Colour panelEdgeGlow        { 0x88E8C284 };

   #elif MIDIMORF_THEME == 2
    //========================== Modern Producer ===============================
    static const juce::Colour background        { 0xff14181C };
    static const juce::Colour padBackground     { 0xff1E2329 };
    static const juce::Colour padBorder         { 0xff283038 };
    static const juce::Colour accent            { 0xff8AE0C2 };   // mint
    static const juce::Colour buttonFace        { 0xff252B33 };
    static const juce::Colour buttonFaceActive  { 0xff8AE0C2 };
    static const juce::Colour buttonBorder      { 0xff3A434E };
    static const juce::Colour textDark          { 0xff0A0E12 };
    static const juce::Colour textLight         { 0xffE8EEF2 };
    static const juce::Colour statusGreen       { 0xff6EE0A0 };
    static const juce::Colour statusAmber       { 0xffE8C56E };   // mustard
    static const juce::Colour crosshair         { 0x40FFFFFF };
    static const juce::Colour dot               { 0xff8AE0C2 };

    static const juce::Colour panelSubstrateTop    { 0xff1A2028 };
    static const juce::Colour panelSubstrateBottom { 0xff0A0E13 };
    static const juce::Colour panelAcrylicTop      { 0xCC2A3540 };
    static const juce::Colour panelAcrylicBottom   { 0xD81A2530 };
    static const juce::Colour panelEdgeGlow        { 0x888AE0C2 };

   #elif MIDIMORF_THEME == 3
    //========================== Anodized Slate ================================
    static const juce::Colour background        { 0xff1A1E22 };
    static const juce::Colour padBackground     { 0xff252A30 };
    static const juce::Colour padBorder         { 0xff333A42 };
    static const juce::Colour accent            { 0xffFF7044 };   // refined coral-orange
    static const juce::Colour buttonFace        { 0xff2E343A };
    static const juce::Colour buttonFaceActive  { 0xffFF7044 };
    static const juce::Colour buttonBorder      { 0xff454E58 };
    static const juce::Colour textDark          { 0xff101418 };
    static const juce::Colour textLight         { 0xffEBEEF1 };
    static const juce::Colour statusGreen       { 0xff5DD088 };
    static const juce::Colour statusAmber       { 0xffFFAE54 };
    static const juce::Colour crosshair         { 0x40FFFFFF };
    static const juce::Colour dot               { 0xffFF7044 };

    static const juce::Colour panelSubstrateTop    { 0xff1F252B };
    static const juce::Colour panelSubstrateBottom { 0xff0E1216 };
    static const juce::Colour panelAcrylicTop      { 0xCC353F4A };
    static const juce::Colour panelAcrylicBottom   { 0xD8232A33 };
    static const juce::Colour panelEdgeGlow        { 0x885DBED4 };  // machined cyan

   #elif MIDIMORF_THEME == 4
    //========================== Light Grey Console ============================
    // MPC-style brushed light grey panel. Dark pads + dark control surfaces
    // sit on top like real hardware on a console. Orange accent stays so the
    // colour identity carries over from theme 0.
    static const juce::Colour background        { 0xffC8C8C8 };
    static const juce::Colour padBackground     { 0xff2A2A2E };
    static const juce::Colour padBorder         { 0xff3A3A3E };
    static const juce::Colour accent            { 0xffE85D3A };
    static const juce::Colour buttonFace        { 0xff8A8A8A };
    static const juce::Colour buttonFaceActive  { 0xffE85D3A };
    static const juce::Colour buttonBorder      { 0xff6A6A6A };
    static const juce::Colour textDark          { 0xff1A1A1A };
    static const juce::Colour textLight         { 0xffE0E0E0 };
    static const juce::Colour statusGreen       { 0xff4CAF50 };
    static const juce::Colour statusAmber       { 0xffFF9800 };
    static const juce::Colour crosshair         { 0x40000000 };   // dark on light bg
    static const juce::Colour dot               { 0xffE85D3A };

    // Light substrate; the "acrylic" + sheen layers become very subtle dark
    // hazes so the panel still has gradient depth without going purple.
    static const juce::Colour panelSubstrateTop    { 0xffDADAD8 };
    static const juce::Colour panelSubstrateBottom { 0xffB4B4B0 };
    static const juce::Colour panelAcrylicTop      { 0x33606060 };
    static const juce::Colour panelAcrylicBottom   { 0x55404040 };
    static const juce::Colour panelEdgeGlow        { 0x88505050 };

   #else
    #error "MIDIMORF_THEME must be 0, 1, 2, 3, or 4"
   #endif
}
