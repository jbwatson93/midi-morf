#pragma once

#include <JuceHeader.h>
#include <memory>

// Reports the device's tilt on the message thread, about 60 times a second.
// Written once per platform, in Platform_ios.mm and Platform_android.cpp.
class AccelerometerManager
{
public:
    struct Listener
    {
        virtual ~Listener() = default;
        virtual void tiltUpdated (float tiltX, float tiltY) = 0;
    };

    AccelerometerManager();
    ~AccelerometerManager();

    void setListener (Listener* l) { listener = l; }

    void start();
    void stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
    Listener* listener = nullptr;
};
