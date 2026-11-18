# Ableton Link

Midi-Morf syncs tempo, bar line and (optionally) start/stop with other apps
over Ableton Link. Code: `MidiMorfApp/Source/LinkSync.h`, `LinkSync_ios.mm`
(LinkKit), `LinkSync_android.cpp` (Ableton's open-source Link), `LinkSync.cpp`
(stub for builds without either), and `LocalSequencers::startOnLink` /
`followLink`.

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

## Getting Link (Android)

Android uses Ableton's open-source C++ Link. It is GPLv2+, or proprietary under
a license from Ableton (link-devs@ableton.com). A Play release must either
publish the app's source under the GPL or have that license. It is not in the
repo until that's settled; git ignores the folder.

```
git clone --recurse-submodules https://github.com/Ableton/link.git MidiMorfApp/ThirdParty/link
```

The Android exporter already has the header paths, `LINK_PLATFORM_LINUX=1` and
the network permissions; re-save the .jucer and build. Android has no Ableton
pane, so the Link button opens a callout with the "Ableton Link" and "Start
Stop Sync" switches, saved in `Link.settings`. The app holds a Wi-Fi multicast
lock while Link is on, because many phones drop multicast without one.

## Apple's multicast entitlement

Link finds other apps with multicast, which on iOS 14+ needs the
`com.apple.developer.networking.multicast` entitlement. Request it at
https://developer.apple.com/contact/request/networking-multicast. Once Apple
grants it, set "Networking Multicast Capability" on in the Projucer iOS
exporter (`networkingMulticast="1"`) and re-save.

## Rules from Ableton's UI guidelines

- Call it "Ableton Link" in full, with its state: Enabled or Disabled.
- Open Ableton's own settings pane (`ABLLinkSettingsViewController`) from it
  on iOS.
- Keep Ableton's in-app join/leave notifications (they come with the pane).
- Never sell Link as an in-app purchase.
