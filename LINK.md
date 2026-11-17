# Ableton Link

Midi-Morf syncs tempo, bar line and (optionally) start/stop with other apps
over Ableton Link. Code: `MidiMorfApp/Source/LinkSync.h`, `LinkSync_ios.mm`
(LinkKit), `LinkSync.cpp` (stub for Android and for builds without LinkKit),
and `LocalSequencers::startOnLink` / `followLink`.

## Getting LinkKit (iOS)

LinkKit's license doesn't allow passing the SDK on, so it is not in the repo.

1. Download `LinkKit.zip` from a full (not pre-release) release at
   https://github.com/Ableton/LinkKit/releases and read its `LICENSE.md`.
2. Lay it out like this (git ignores the folder):

```
MidiMorfApp/ThirdParty/LinkKit/
  include/                      <- LinkKit.xcframework/ios-arm64/Headers
  lib/iphoneos/libLinkKit.a     <- LinkKit.xcframework/ios-arm64/
  lib/iphonesimulator/libLinkKit.a <- LinkKit.xcframework/ios-arm64_x86_64-simulator/
  LinkKitResources.bundle
```

3. Re-save the .jucer in Projucer.

## Apple's multicast entitlement

Link finds other apps with multicast, which on iOS 14+ needs the
`com.apple.developer.networking.multicast` entitlement. Request it at
https://developer.apple.com/contact/request/networking-multicast. Once Apple
grants it, set "Networking Multicast Capability" on in the Projucer iOS
exporter (`networkingMulticast="1"`) and re-save.

## Rules from Ableton's UI guidelines

- Call it "Ableton Link" in full, with its state: Enabled or Disabled.
- Open Ableton's own settings pane (`ABLLinkSettingsViewController`) from it.
- Keep Ableton's in-app join/leave notifications (they come with the pane).
- Never sell Link as an in-app purchase.
