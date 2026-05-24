#pragma once

#include <JuceHeader.h>

#if JUCE_IOS
#import <CoreMotion/CoreMotion.h>
#endif

class AccelerometerManager
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void tiltUpdated (float tiltX, float tiltY) = 0;
    };

    AccelerometerManager() {}
    ~AccelerometerManager() { stop(); }

    void setListener (Listener* l) { listener = l; }

    void start()
    {
       #if JUCE_IOS
        if (motionManager != nil)
            return;

        motionManager = [[CMMotionManager alloc] init];

        if (! motionManager.deviceMotionAvailable)
        {
            DBG ("AccelerometerManager: device motion not available");
            motionManager = nil;
            return;
        }

        motionManager.deviceMotionUpdateInterval = 1.0 / 60.0;

        [motionManager startDeviceMotionUpdatesToQueue:[NSOperationQueue mainQueue]
            withHandler:^(CMDeviceMotion* motion, NSError* error)
            {
                if (error != nil || motion == nil)
                    return;

                // gravity gives us a nice filtered tilt vector
                // x: left/right tilt (-1 to 1)
                // y: forward/back tilt (-1 to 1)
                float tiltX = (float) motion.gravity.x;
                float tiltY = (float) motion.gravity.y;

                if (listener != nullptr)
                    listener->tiltUpdated (tiltX, tiltY);
            }];

        DBG ("AccelerometerManager: started");
       #endif
    }

    void stop()
    {
       #if JUCE_IOS
        if (motionManager != nil)
        {
            [motionManager stopDeviceMotionUpdates];
            motionManager = nil;
            DBG ("AccelerometerManager: stopped");
        }
       #endif
    }

private:
    Listener* listener = nullptr;

   #if JUCE_IOS
    CMMotionManager* motionManager = nil;
   #endif
};
