#include "LinkSync.h"

// The stub: every platform but iOS, and iOS built without LinkKit. The real
// one is LinkSync_ios.mm.
#if ! (JUCE_IOS && __has_include(<ABLLink.h>))

struct LinkSync::Impl {};

LinkSync::LinkSync() = default;
LinkSync::~LinkSync() = default;

bool LinkSync::isAvailable()                  { return false; }
bool LinkSync::isEnabled() const              { return false; }
int  LinkSync::numPeers() const               { return 0; }
void LinkSync::showSettings()                 {}
void LinkSync::setActive (bool)               {}
void LinkSync::requestTempo (double)          {}
void LinkSync::setOutputLatencySeconds (double) {}
void LinkSync::setBufferHostTimeNs (uint64_t) {}
bool LinkSync::beginBlock (Block&)            { return false; }
void LinkSync::requestBeatAtBlockStart (double) {}
void LinkSync::setPlayingAtBeat (bool, double) {}
void LinkSync::endBlock()                     {}

#endif
