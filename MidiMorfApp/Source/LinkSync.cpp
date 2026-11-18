#include "LinkSync.h"

// The stub, for a build without Link's SDK. The real ones are LinkSync_ios.mm
// and LinkSync_android.cpp.
#if ! ((JUCE_IOS && __has_include(<ABLLink.h>)) || (JUCE_ANDROID && __has_include(<ableton/Link.hpp>)))

struct LinkSync::Impl {};

LinkSync::LinkSync() = default;
LinkSync::~LinkSync() = default;

bool LinkSync::isAvailable()                  { return false; }
bool LinkSync::isEnabled() const              { return false; }
int  LinkSync::numPeers() const               { return 0; }
void LinkSync::showSettings (juce::Component&) {}
void LinkSync::setActive (bool)               {}
void LinkSync::requestTempo (double)          {}
void LinkSync::setOutputLatencySeconds (double) {}
void LinkSync::setBufferHostTimeNs (uint64_t) {}
bool LinkSync::beginBlock (Block&)            { return false; }
void LinkSync::requestBeatAtBlockStart (double) {}
void LinkSync::setPlayingAtBeat (bool, double) {}
void LinkSync::endBlock()                     {}

#endif
