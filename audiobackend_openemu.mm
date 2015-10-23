#include "audiobackend_openemu.h"
#import "ReicastGameCore.h"
#import <OpenEmuBase/OERingBuffer.h>

static void openemu_init()
{
    
}

static u32 openemu_push(void* frame, u32 samples, bool wait)
{
//    GET_CURRENT_OR_RETURN(0);
    [[_current ringBufferAtIndex:0] write:frame maxLength:(size_t)samples*4];
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
