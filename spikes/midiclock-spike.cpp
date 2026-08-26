// ─────────────────────────────────────────────────────────────────────────────
// MidiMorf — MIDI Clock timing spike
//
// Answers one question: is DAW MIDI clock into the server tight enough to run
// the Creative Sequencer Pack engines there, instead of building a plugin?
//
// Creates two virtual CoreMIDI endpoints:
//   "MidiMorf Sync"      — a destination. Enable Sync to this in your DAW.
//   "MidiMorf Spike Out" — a source. Record from this to check the round trip.
//
// Counts 0xF8 clock (24 PPQN), fires a note every 6 ticks (one 16th), and
// measures how far each 16th lands from where it should.
//
// The key statistic is JITTER: residuals from a least-squares line fitted to
// the step arrival times. That deliberately separates constant tempo offset and
// slow drift (both correctable) from genuine wander (not correctable).
//
// Build:  clang++ -std=c++17 -O2 midiclock-spike.cpp \
//                 -framework CoreMIDI -framework CoreFoundation -o midiclock-spike
// Run:    ./midiclock-spike        (Ctrl-C for the final report)
// ─────────────────────────────────────────────────────────────────────────────

#include <CoreMIDI/CoreMIDI.h>
#include <CoreFoundation/CoreFoundation.h>
#include <mach/mach_time.h>

#include <atomic>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>

#pragma clang diagnostic ignored "-Wdeprecated-declarations"  // MIDIPacketList API

// ── Config ───────────────────────────────────────────────────────────────────
static constexpr int  TICKS_PER_STEP = 6;    // 24 PPQN / 4 = 16th notes
static constexpr int  GATE_TICKS     = 3;    // note-off after a 32nd
static constexpr int  NOTE           = 36;   // C1 — lands on a kick in most kits
static constexpr int  VELOCITY       = 100;
static constexpr int  MAX_STEPS      = 200000;
static constexpr int  REPORT_EVERY   = 16;   // one bar of 16ths
static constexpr int  FIT_WINDOW     = 64;   // 4 bars — rolling, so tempo ramps don't
                                             // invalidate the linear fit (see README)

// ── State ────────────────────────────────────────────────────────────────────
static MIDIClientRef    gClient     = 0;
static MIDIEndpointRef  gVirtualIn  = 0;   // DAW -> us (clock)
static MIDIEndpointRef  gVirtualOut = 0;   // us -> DAW (notes)

static mach_timebase_info_data_t gTimebase;

static uint64_t gStepNs[MAX_STEPS];
static int      gStepCount   = 0;
static uint64_t gLastTickNs  = 0;
static double   gTickIntSum  = 0.0;   // running mean of tick intervals -> tempo
static int      gTickIntN    = 0;
static int      gTickCount   = 0;
static int      gGateCounter = -1;
static bool     gRunning     = false;
static int      gLastReport  = 0;

static double   gLeadSumMs   = 0.0;   // pkt->timeStamp minus actual arrival
static double   gLeadMinMs   = 1e18;
static double   gLeadMaxMs   = -1e18;
static int      gLeadN       = 0;
static int      gZeroStamps  = 0;

static std::atomic<bool> gQuit { false };

static inline uint64_t machToNs (uint64_t t)
{
    return t * gTimebase.numer / gTimebase.denom;
}

static inline uint64_t nowNs()
{
    return machToNs (mach_absolute_time());
}

// ── MIDI out ─────────────────────────────────────────────────────────────────
// Stamp output for the tick's own host time. If the DAW schedules its clock
// ahead, CoreMIDI delivers our note at exactly that instant. If the stamp is
// already past, CoreMIDI sends immediately — never worse than timestamp 0.
static void sendBytes (const uint8_t* data, int n, MIDITimeStamp when)
{
    if (gVirtualOut == 0)
        return;

    Byte buffer[128];
    auto* list = reinterpret_cast<MIDIPacketList*> (buffer);
    auto* pkt  = MIDIPacketListInit (list);
    pkt = MIDIPacketListAdd (list, sizeof (buffer), pkt, when, n, data);

    if (pkt != nullptr)
        MIDIReceived (gVirtualOut, list);
}

static void noteOn  (MIDITimeStamp t) { const uint8_t m[3] = { 0x90, NOTE, VELOCITY }; sendBytes (m, 3, t); }
static void noteOff (MIDITimeStamp t) { const uint8_t m[3] = { 0x80, NOTE, 0 };        sendBytes (m, 3, t); }

// ── Stats ────────────────────────────────────────────────────────────────────
struct Fit { double slopeNs, jitterRmsMs, jitterMaxMs, driftMs; };

// Least-squares line through (index, stepNs). Residuals are the jitter.
static Fit analyse()
{
    Fit f { 0, 0, 0, 0 };
    if (gStepCount < 3)
        return f;

    const int n     = (gStepCount < FIT_WINDOW) ? gStepCount : FIT_WINDOW;
    const int first = gStepCount - n;
    const double base = (double) gStepNs[first];

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (int i = 0; i < n; ++i)
    {
        const double x = i;
        const double y = (double) gStepNs[first + i] - base;
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }

    const double denom = n * sxx - sx * sx;
    if (denom == 0.0)
        return f;

    const double slope     = (n * sxy - sx * sy) / denom;
    const double intercept = (sy - slope * sx) / n;

    double sumSq = 0.0, maxAbs = 0.0;
    for (int i = 0; i < n; ++i)
    {
        const double predicted = slope * i + intercept;
        const double actual    = (double) gStepNs[first + i] - base;
        const double resid     = actual - predicted;
        sumSq += resid * resid;
        maxAbs = std::fmax (maxAbs, std::fabs (resid));
    }

    f.slopeNs      = slope;
    f.jitterRmsMs  = std::sqrt (sumSq / n) / 1e6;
    f.jitterMaxMs  = maxAbs / 1e6;

    // Cumulative drift: where the last step landed vs. a perfect grid at the
    // tempo implied by the incoming clock.
    if (gTickIntN > 0)
    {
        const double idealStepNs = (gTickIntSum / gTickIntN) * TICKS_PER_STEP;
        const double idealLast   = idealStepNs * (n - 1);
        const double actualLast  = (double) gStepNs[first + n - 1] - base;
        f.driftMs = (actualLast - idealLast) / 1e6;
    }

    return f;
}

static double currentBpm()
{
    if (gTickIntN == 0)
        return 0.0;
    const double meanTickNs = gTickIntSum / gTickIntN;
    return 60e9 / (meanTickNs * 24.0);
}

static void printReport (const char* title)
{
    const auto f = analyse();

    printf ("\n──────────────────────────────────────────────────────────\n");
    printf ("  %s\n", title);
    printf ("──────────────────────────────────────────────────────────\n");

    if (gStepCount < 3)
    {
        printf ("  Not enough clock received (%d steps). Is Sync enabled?\n\n", gStepCount);
        return;
    }

    const double bpm       = currentBpm();
    const double stepMs    = f.slopeNs / 1e6;
    const double idealMs   = (gTickIntN > 0) ? (gTickIntSum / gTickIntN) * TICKS_PER_STEP / 1e6 : 0.0;

    printf ("  Steps measured     %d 16ths (%.1f bars)\n", gStepCount, gStepCount / 16.0);
    printf ("  Tempo from clock   %.3f BPM\n", bpm);
    printf ("  16th interval      %.3f ms measured  /  %.3f ms ideal\n", stepMs, idealMs);
    printf ("\n");
    printf ("  JITTER (rms)       %.3f ms   <-- the number that decides this\n", f.jitterRmsMs);
    printf ("  JITTER (worst)     %.3f ms\n", f.jitterMaxMs);
    printf ("  Cumulative drift   %+.3f ms over %d steps (last %d fitted)\n",
            f.driftMs, gStepCount, (gStepCount < FIT_WINDOW ? gStepCount : FIT_WINDOW));
    printf ("\n");

    if (gZeroStamps > 0 && gLeadN == 0)
    {
        printf ("  CLOCK STAMPING     timestamp 0 on all %d ticks -- DAW says \"deliver now\".\n", gZeroStamps);
        printf ("                     Jitter above IS real delivery jitter.\n");
        printf ("                     Engine should fire immediately; no scheduling to exploit.\n");
    }
    else if (gLeadN > 0)
    {
        const double meanLead = gLeadSumMs / gLeadN;
        printf ("  CLOCK STAMPING     scheduled, %+.3f ms mean lead (range %+.3f to %+.3f)\n",
                meanLead, gLeadMinMs, gLeadMaxMs);
        if (meanLead > 0.5)
        {
            printf ("                     DAW schedules AHEAD -- we can schedule too.\n");
            printf ("                     Engine should stamp notes for the tick's host time:\n");
            printf ("                     sample-accurate output, as good as a plugin.\n");
        }
        else
        {
            printf ("                     Stamps arrive at/after their time -- no lead to exploit.\n");
            printf ("                     Jitter above reflects intent, not delivery. Trust the\n");
            printf ("                     recorded round trip in the DAW over this number.\n");
        }
        if (gZeroStamps > 0)
            printf ("                     (%d ticks also arrived unstamped)\n", gZeroStamps);
    }
    printf ("\n");

    const char* verdict;
    if      (f.jitterRmsMs < 1.0) verdict = "EXCELLENT — server-side engines are fine. Plan stands.";
    else if (f.jitterRmsMs < 3.0) verdict = "GOOD — comfortably inside hardware-sequencer territory.";
    else if (f.jitterRmsMs < 8.0) verdict = "ACCEPTABLE — audible but musical. Record Quantization cleans capture.";
    else                          verdict = "POOR — escalate to Ableton Link, or reconsider the plugin.";

    printf ("  VERDICT: %s\n", verdict);
    printf ("──────────────────────────────────────────────────────────\n\n");
}

// ── Clock handling ───────────────────────────────────────────────────────────
static void resetCounters()
{
    gStepCount  = 0;
    gTickCount  = 0;
    gTickIntSum = 0.0;
    gTickIntN   = 0;
    gLastTickNs = 0;
    gGateCounter = -1;
    gLastReport = 0;
    gLeadSumMs = 0.0; gLeadMinMs = 1e18; gLeadMaxMs = -1e18; gLeadN = 0; gZeroStamps = 0;
}

static void handleClock (uint64_t ts, MIDITimeStamp raw)
{
    if (! gRunning)
        return;

    if (gLastTickNs != 0)
    {
        const double interval = (double) (ts - gLastTickNs);
        // Ignore absurd intervals (transport scrubs, pauses)
        if (interval > 1e5 && interval < 1e9)
        {
            gTickIntSum += interval;
            ++gTickIntN;
        }
    }
    gLastTickNs = ts;

    if (gGateCounter > 0 && --gGateCounter == 0)
        noteOff (raw);

    if (gTickCount % TICKS_PER_STEP == 0)
    {
        if (gStepCount < MAX_STEPS)
            gStepNs[gStepCount++] = ts;

        noteOn (raw);
        gGateCounter = GATE_TICKS;

        if (gStepCount - gLastReport >= REPORT_EVERY)
        {
            gLastReport = gStepCount;
            const auto f = analyse();
            printf ("  bar %-4d  %6.2f BPM   jitter %5.3f ms rms / %5.3f ms peak   drift %+7.3f ms\n",
                    gStepCount / 16, currentBpm(), f.jitterRmsMs, f.jitterMaxMs, f.driftMs);
            fflush (stdout);
        }
    }

    ++gTickCount;
}

static void readProc (const MIDIPacketList* list, void*, void*)
{
    const MIDIPacket* pkt = &list->packet[0];

    for (unsigned i = 0; i < list->numPackets; ++i)
    {
        // Prefer CoreMIDI's own timestamp; fall back to arrival time if the
        // sender stamped 0 ("deliver now"), which many DAWs do.
        const uint64_t arrival = nowNs();
        const uint64_t ts = (pkt->timeStamp != 0) ? machToNs (pkt->timeStamp) : arrival;

        if (pkt->timeStamp == 0)
        {
            ++gZeroStamps;
        }
        else
        {
            const double leadMs = ((double) ts - (double) arrival) / 1e6;
            gLeadSumMs += leadMs;
            gLeadMinMs = std::fmin (gLeadMinMs, leadMs);
            gLeadMaxMs = std::fmax (gLeadMaxMs, leadMs);
            ++gLeadN;
        }

        for (unsigned j = 0; j < pkt->length; ++j)
        {
            switch (pkt->data[j])
            {
                case 0xF8: handleClock (ts, pkt->timeStamp); break;

                case 0xFA:  // start
                    printf ("\n  ▶ START\n");
                    resetCounters();
                    gRunning = true;
                    break;

                case 0xFB:  // continue
                    printf ("\n  ▶ CONTINUE\n");
                    gRunning = true;
                    gLastTickNs = 0;
                    break;

                case 0xFC:  // stop
                    gRunning = false;
                    if (gGateCounter > 0) { noteOff (0); gGateCounter = -1; }
                    printReport ("STOP — segment report");
                    break;

                default: break;
            }
        }

        pkt = MIDIPacketNext (pkt);
    }
}

// ── Main ─────────────────────────────────────────────────────────────────────
static void onSigint (int) { gQuit.store (true); }

int main()
{
    mach_timebase_info (&gTimebase);

    OSStatus err = MIDIClientCreate (CFSTR ("MidiMorf Spike"), nullptr, nullptr, &gClient);
    if (err != noErr) { printf ("MIDIClientCreate failed: %d\n", (int) err); return 1; }

    err = MIDIDestinationCreate (gClient, CFSTR ("MidiMorf Sync"), readProc, nullptr, &gVirtualIn);
    if (err != noErr) { printf ("MIDIDestinationCreate failed: %d\n", (int) err); return 1; }

    err = MIDISourceCreate (gClient, CFSTR ("MidiMorf Spike Out"), &gVirtualOut);
    if (err != noErr) { printf ("MIDISourceCreate failed: %d\n", (int) err); return 1; }

    printf ("\n");
    printf ("  MidiMorf — MIDI Clock timing spike\n");
    printf ("  ══════════════════════════════════\n\n");
    printf ("  Two virtual ports are now live:\n");
    printf ("    · MidiMorf Sync        (destination — send clock here)\n");
    printf ("    · MidiMorf Spike Out   (source — notes come out here)\n\n");
    printf ("  In Ableton Live:\n");
    printf ("    1. Preferences > Link/Tempo/MIDI\n");
    printf ("    2. Find \"MidiMorf Sync\" in the Output list, enable  Sync\n");
    printf ("    3. Find \"MidiMorf Spike Out\" in the Input list, enable  Track\n");
    printf ("    4. New MIDI track, input = MidiMorf Spike Out, monitor In, arm it\n");
    printf ("    5. Turn Record Quantization OFF (Edit menu) — we want the raw truth\n");
    printf ("    6. Press play. Record a few bars.\n\n");
    printf ("  Live reports per bar below. Ctrl-C for the full report.\n\n");
    fflush (stdout);

    signal (SIGINT, onSigint);

    while (! gQuit.load())
        CFRunLoopRunInMode (kCFRunLoopDefaultMode, 0.25, false);

    if (gGateCounter > 0)
        noteOff (0);

    printReport ("FINAL REPORT");

    MIDIEndpointDispose (gVirtualIn);
    MIDIEndpointDispose (gVirtualOut);
    MIDIClientDispose (gClient);
    return 0;
}
