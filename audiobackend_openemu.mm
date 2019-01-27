#import "audiobackend_openemu.h"
#import "ReicastGameCore.h"
#include <mach/mach_time.h>
#import <OpenEmuBase/OERingBuffer.h>
#import <chrono>

#define SAMPLERATE  44100

uint waitime, lastwaitime;

mach_timebase_info_data_t info;
uint64_t start;
uint64_t duration;

double durationMultiplier;

static void openemu_init()
{
    mach_timebase_info(&info);
    start = mach_absolute_time();
    lastwaitime = 0;
    durationMultiplier =  (info.numer/ info.denom) / 1000.0 ;  //Converts mach time to microseconds
}

static u32 openemu_push(void* frame, u32 samples, bool wait)
{
    //Reicast is using the sound buffer as timing.  OpenEmu sound uses multiple
    //  buffers so we have no way to wait that I've found
    //We need to wait for sound to play before returning to the emulator core
    //  we calculate the time that is needed in microseconds to play the current sound
    //  and subtract the time it took since the last sound samples were played
    
    //Calcutale Changes in frame-sound timing.  In case frame interval or samples count changed
    int  SamplesPerFrame = SAMPLERATE / [_current frameInterval] ;
    double PercentOfFrameWithSound = 1.0 * samples / SamplesPerFrame;
    double SingleFrameTime = 1.0 / [_current frameInterval] * 1000000;
    
    //Calculate the maximum amount of time in microseconds it takes to play the samples sent
    uint64_t MaxFrameTime = SingleFrameTime * PercentOfFrameWithSound ;
    
    //Figure out the time since the last partial frame of sound played
    duration = mach_absolute_time() - start;
    
    //start the next epoch
    start = mach_absolute_time();
    
    //Write the sound bytes to the buffer
    [[_current audioBufferAtIndex:0] write:frame maxLength:(size_t)samples * 4];
    
    /* Convert to microeconds */
    duration *= durationMultiplier;
    duration -= lastwaitime;
    
    //If duration was less than max time for sound play, subtract the duration from the max time, and wait the remainder
    //   else no wait is neccessary
    if (duration < MaxFrameTime)
        waitime = (uint)(MaxFrameTime - duration);
    else
        waitime = 0;
   
    if (wait) {
        usleep(waitime);
        lastwaitime = waitime;
//      printf ("Sound Push Bytes: %i Wait: %11d", samples, waitime);
    } else {
        lastwaitime = 0;
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
