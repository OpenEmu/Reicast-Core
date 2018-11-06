#import "audiobackend_openemu.h"
#import "ReicastGameCore.h"
#import <OpenEmuBase/OERingBuffer.h>
#import <chrono>

#define SAMPLERATE 44100

int waitime;

static void openemu_init()
{
}

static u32 openemu_push(void* frame, u32 samples, bool wait)
{
    [[_current ringBufferAtIndex:0] write:frame maxLength:(size_t)samples * 4];
    
    waitime = (int)(1000000.00/([_current frameInterval] * 2.5));
    
    //Reicast is using the sound buffer as timing.  OpenEmu sound uses multiple
    //  buffers so we have no way to wait that I've found
    //We need to wait for sound to play before returning to the emulator core
    //  This wait time is just an estimate, and it may be and is most likely TOTALLY incorrect
    if (wait) {
        usleep(waitime);
       // printf ("Sound Push Bytes: %i Wait: %i", samples, waitime);
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
