#include "Haptics.h"
#include <JuceHeader.h>

#if JUCE_IOS
 #import <UIKit/UIKit.h>
#endif

namespace MidiMorfHaptics
{
    void impactMedium()
    {
       #if JUCE_IOS
        if (@available (iOS 10.0, *))
        {
            UIImpactFeedbackGenerator* gen =
                [[UIImpactFeedbackGenerator alloc]
                    initWithStyle: UIImpactFeedbackStyleMedium];
            [gen prepare];
            [gen impactOccurred];
        }
       #endif
    }
}
