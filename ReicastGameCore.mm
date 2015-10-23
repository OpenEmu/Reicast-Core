/*
 Copyright (c) 2013, OpenEmu Team

 Redistribution and use in source and binary forms, with or without
 modification, are permitted provided that the following conditions are met:
     * Redistributions of source code must retain the above copyright
       notice, this list of conditions and the following disclaimer.
     * Redistributions in binary form must reproduce the above copyright
       notice, this list of conditions and the following disclaimer in the
       documentation and/or other materials provided with the distribution.
     * Neither the name of the OpenEmu Team nor the
       names of its contributors may be used to endorse or promote products
       derived from this software without specific prior written permission.

 THIS SOFTWARE IS PROVIDED BY OpenEmu Team ''AS IS'' AND ANY
 EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 DISCLAIMED. IN NO EVENT SHALL OpenEmu Team BE LIABLE FOR ANY
 DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
  (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#import "ReicastGameCore.h"
#import <OpenEmuBase/OERingBuffer.h>
#import <OpenGL/gl.h>

#include "oslib/audiostream.h"
#include "audiobackend_openemu.h"
#include "types.h"
#include <sys/stat.h>

#define SAMPLERATE 44100
#define SIZESOUNDBUFFER 44100 / 60 * 4

@interface ReicastGameCore () <OEDCSystemResponderClient>
{
    uint16_t *_soundBuffer;
    int videoWidth, videoHeight;
    NSString *romPath;
}
@end

__weak ReicastGameCore *_current;

@implementation ReicastGameCore

- (id)init
{
    self = [super init];

    if(self)
    {
        videoHeight = 480;
        videoWidth = 640;
        _soundBuffer = (uint16_t *)malloc(SIZESOUNDBUFFER * sizeof(uint16_t));
        memset(_soundBuffer, 0, SIZESOUNDBUFFER * sizeof(uint16_t));
    }
    
    _current = self;
    return self;
}

- (void)dealloc
{
    free(_soundBuffer);
}

# pragma mark - Execution

int msgboxf(const wchar* text,unsigned int type,...)
{
    va_list args;
    
    wchar temp[2048];
    va_start(args, type);
    vsprintf(temp, text, args);
    va_end(args);
    
    puts(temp);
    return 0;
}

int darw_printf(const wchar* text,...) {
    va_list args;
    
    wchar temp[2048];
    va_start(args, text);
    vsprintf(temp, text, args);
    va_end(args);
    
    NSLog(@"%s", temp);
    
    return 0;
}

int dc_init(int argc,wchar* argv[]);
void dc_run();
void dc_term();

volatile bool has_init = false;
- (void)emuthread {
    settings.profile.run_counts = 0;

    mkdir([[self batterySavesDirectoryPath] UTF8String], 0755);
    set_user_config_dir([[self batterySavesDirectoryPath] UTF8String]);
    set_user_data_dir([[self biosDirectoryPath] UTF8String]);

    char* argv[] = { "reicast", (char*)[romPath UTF8String] };
    dc_init(2,argv);

    has_init = true;

    dc_run();
}

- (BOOL)loadFileAtPath:(NSString *)path error:(NSError **)error
{
    romPath = path;
    return YES;
}

- (void)setupEmulation
{
    screen_width = videoWidth;
    screen_height = videoHeight;
    RegisterAudioBackend(&audiobackend_openemu);
}

- (void)stopEmulation
{
    [super stopEmulation];
    dc_term();
}

- (void)resetEmulation
{
    
}

- (void)executeFrame
{
    if (!has_init) {
        gles_init();

        [NSThread detachNewThreadSelector:@selector(emuthread) toTarget:self withObject:nil];

        while (!has_init) {;}
    }
    
    while (rend_single_frame() == 0) {}
}

# pragma mark - Video

extern int screen_width,screen_height;
bool rend_single_frame();
bool gles_init();

void os_SetWindowText(const char * text) {
    puts(text);
}

void os_DoEvents() {
    
}


void UpdateInputState(u32 port) {
    
}

void os_CreateWindow() {
    
}

void* libPvr_GetRenderTarget() {
    return 0;
}

void* libPvr_GetRenderSurface() {
    return 0;
}

bool gl_init(void*, void*) {
    return true;
}

void gl_term() {
    
}

void gl_swap() {
    
}

- (OEGameCoreRendering)gameCoreRendering
{
    return OEGameCoreRenderingOpenGL3Video;
}

- (OEIntSize)bufferSize
{
    return OEIntSizeMake(videoWidth, videoHeight);
}

//- (OEIntSize)aspectSize
//{
//    return OEIntSizeMake(16, 9);
//}

- (NSTimeInterval)frameInterval
{
    return 60;
}

# pragma mark - Audio

- (NSUInteger)channelCount
{
    return 2;
}

- (double)audioSampleRate
{
    return SAMPLERATE;
}

# pragma mark - Save States

// Save State is not implemented by Reicast at this time
- (BOOL)saveStateToFileAtPath: (NSString *) fileName
{
    return NO;
}

- (BOOL)loadStateFromFileAtPath: (NSString *) fileName
{
    return NO;
}

# pragma mark - Input

int get_mic_data(u8* buffer) { return 0; }
int push_vmu_screen(u8* buffer) { return 0; }

u16 kcode[4] = { 0xFFFF };
u32 vks[4];
s8 joyx[4],joyy[4];
u8 rt[4],lt[4];

enum DCPad
{
    Btn_C		= 1,
    Btn_B		= 1<<1,
    Btn_A		= 1<<2,
    Btn_Start	= 1<<3,
    DPad_Up		= 1<<4,
    DPad_Down	= 1<<5,
    DPad_Left	= 1<<6,
    DPad_Right	= 1<<7,
    Btn_Z		= 1<<8,
    Btn_Y		= 1<<9,
    Btn_X		= 1<<10,
    Btn_D		= 1<<11,
    DPad2_Up	= 1<<12,
    DPad2_Down	= 1<<13,
    DPad2_Left	= 1<<14,
    DPad2_Right	= 1<<15,
    
    Axis_LT= 0x10000,
    Axis_RT= 0x10001,
    Axis_X= 0x20000,
    Axis_Y= 0x20001,
};

void handle_key(int dckey, int state, int player)
{
    if (state)
        kcode[player-1] &= ~dckey;
    else
        kcode[player-1] |= dckey;
}

void handle_trig(u8* dckey, int state, int player)
{
    if (state)
        dckey[player-1] = 255;
    else
        dckey[player-1] = 0;
}

- (oneway void)didMoveDCJoystickDirection:(OEDCButton)button withValue:(CGFloat)value forPlayer:(NSUInteger)player
{
    player -= 1;
    switch (button)
    {
        case OEDCAnalogUp:
            joyy[player] = value * INT8_MIN;
            break;
        case OEDCAnalogDown:
            joyy[player] = value * INT8_MAX;
            break;
        case OEDCAnalogLeft:
            joyx[player] = value * INT8_MIN;
            break;
        case OEDCAnalogRight:
            joyx[player] = value * INT8_MAX;
            break;
        default:
            break;
    }
}

-(oneway void)didPushDCButton:(OEDCButton)button forPlayer:(NSUInteger)player
{
    switch (button) {
        case OEDCButtonUp:
            handle_key(DPad_Up, 1, (int)player);
            break;
        case OEDCButtonDown:
            handle_key(DPad_Down, 1, (int)player);
            break;
        case OEDCButtonLeft:
            handle_key(DPad_Left, 1, (int)player);
            break;
        case OEDCButtonRight:
            handle_key(DPad_Right, 1, (int)player);
            break;
        case OEDCButtonA:
            handle_key(Btn_A, 1, (int)player);
            break;
        case OEDCButtonB:
            handle_key(Btn_B, 1, (int)player);
            break;
        case OEDCButtonX:
            handle_key(Btn_X, 1, (int)player);
            break;
        case OEDCButtonY:
            handle_key(Btn_Y, 1, (int)player);
            break;
        case OEDCAnalogL:
            handle_trig(lt, 1, (int)player);
            break;
        case OEDCAnalogR:
            handle_trig(rt, 1, (int)player);
            break;
        case OEDCButtonStart:
            handle_key(Btn_Start, 1, (int)player);
            break;
        default:
            break;
    }
}

- (oneway void)didReleaseDCButton:(OEDCButton)button forPlayer:(NSUInteger)player
{
    switch (button) {
        case OEDCButtonUp:
            handle_key(DPad_Up, 0, (int)player);
            break;
        case OEDCButtonDown:
            handle_key(DPad_Down, 0, (int)player);
            break;
        case OEDCButtonLeft:
            handle_key(DPad_Left, 0, (int)player);
            break;
        case OEDCButtonRight:
            handle_key(DPad_Right, 0, (int)player);
            break;
        case OEDCButtonA:
            handle_key(Btn_A, 0, (int)player);
            break;
        case OEDCButtonB:
            handle_key(Btn_B, 0, (int)player);
            break;
        case OEDCButtonX:
            handle_key(Btn_X, 0, (int)player);
            break;
        case OEDCButtonY:
            handle_key(Btn_Y, 0, (int)player);
            break;
        case OEDCAnalogL:
            handle_trig(lt, 0, (int)player);
            break;
        case OEDCAnalogR:
            handle_trig(rt, 0, (int)player);
            break;
        case OEDCButtonStart:
            handle_key(Btn_Start, 0, (int)player);
            break;
        default:
            break;
    }
}

@end
