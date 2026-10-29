#import "audiobackend_openemu.h"
#import "ReicastGameCore.h"
#import <OpenEmuBase/OERingBuffer.h>
#import <time.h>

#include "hw/pvr/Renderer_if.h"

#define SAMPLERATE  44100

// Time (in nanoseconds, CLOCK_UPTIME_RAW) at which the audio written so far has
// been played. Zero until the first push.
static uint64_t pushDeadline;

static void openemu_init()
{
    pushDeadline = 0;
    // Emulation is paced here, so it must not also wait for the presentation
    // loop (see rend_async in hw/pvr/Renderer_if.cpp).
    rend_async = true;
}

// Reicast emulates on its own thread and uses the audio backend as its frame
// limiter: the blocking backends wait until the samples they queued have been
// played, and that wait is what keeps the emulator at real time. OpenEmu only
// paces the thread that presents finished frames, so the emulation thread has
// to be throttled here.
//
// Throttling to a fixed 44100 samples/s would be fragile, because that rate
// only matches the audio device on average and OERingBuffer refuses to read at
// all while fewer than twice the requested bytes are queued - any shortfall
// shows up as periodic dropouts. A single overlong sleep is enough to cause
// them, which is what the rhythmic crackle was: the previous implementation
// slept the chunk duration per push and never made up for usleep overshooting,
// so production ran about 2% below the device rate and the buffer was pinned
// at the underflow threshold.
//
// So instead the deadline advances by the duration of the chunk plus a
// correction towards a target fill level of the ring buffer: queued too little
// and the next chunk is treated as slightly shorter, queued too much and it is
// treated as slightly longer. The emulation rate thereby follows the device
// clock without drifting, and the queue keeps enough audio for
// AVAudioSourceNode's render callbacks.
static u32 openemu_push(void* frame, u32 samples, bool wait)
{
    OERingBuffer *ring = (OERingBuffer *)[_current audioBufferAtIndex:0];

    //Write the sound bytes to the buffer
    [[_current audioBufferAtIndex:0] write:frame maxLength:(size_t)samples * 4];

    const int64_t nsPerSample = 1000000000LL / SAMPLERATE;
    // 16-bit stereo, so four bytes per sample frame. The emulation loses a few
    // percent of its time to the render thread, and the level settles where the
    // resulting correction covers that, a little below the target - hence half
    // the buffer (about 90 ms) rather than a quarter. That still leaves the
    // usual case well clear of OERingBuffer's read threshold of two 2048-byte
    // slices.
    const int64_t targetBytes = (int64_t)ring.length / 2;
    const int64_t errorFrames = ((int64_t)ring.availableBytes - targetBytes) / 4;
    // Correct 5% of the level error per chunk. With 512 samples per chunk the
    // settling time is around a second, slow enough not to modulate the rate.
    const int64_t correction = errorFrames * nsPerSample / 20;

    const uint64_t now = clock_gettime_nsec_np(CLOCK_UPTIME_RAW);
    const int64_t chunk = (int64_t)samples * nsPerSample;

    // Start a new schedule on the first push, and resynchronise when the
    // deadline is far behind (paused emulation, scheduling hiccup); catching up
    // by running the emulator as fast as possible would overflow the buffers.
    if (pushDeadline == 0 || now > pushDeadline + 100000000ULL)
        pushDeadline = now;

    pushDeadline = (uint64_t)((int64_t)pushDeadline + chunk + correction);

    if (pushDeadline > now)
    {
        uint64_t remaining = pushDeadline - now;
        struct timespec request = { (time_t)(remaining / 1000000000ULL), (long)(remaining % 1000000000ULL) };
        nanosleep(&request, NULL);
    }

	return 1;
}

static void openemu_term() {
}

audiobackend_t audiobackend_openemu = {
		"openemu", // Slug
		"OpenEmu", // Name
		&openemu_init,
		&openemu_push,
		&openemu_term
};
