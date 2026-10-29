#pragma once

namespace MidiMorfHaptics
{
    // Triggers a medium-style impact haptic via UIImpactFeedbackGenerator.
    // No-op on non-iOS targets (the macOS build of this app links the same
    // source). Cheap to call — the generator is allocated, prepared, fired,
    // and released inline.
    void impactMedium();
}
