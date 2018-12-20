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
#import <OpenGL/gl3.h>

#include "oslib/audiostream.h"
#include "audiobackend_openemu.h"

#include "types.h"
#include "rend/rend.h"
#include <sys/stat.h>
#include <functional>

#include "maple_cfg.h"
#include "cfg.h"

#define SAMPLERATE 44100
#define SIZESOUNDBUFFER 44100 / 60 * 4

typedef std::function<void(bool status, const std::string &message, void *cbUserData)> Callback;
void dc_savestate(const std::string &fileName, Callback callback, void *cbUserData);
void dc_loadstate(const std::string &fileName, Callback callback, void *cbUserData);

@interface ReicastGameCore () <OEDCSystemResponderClient>
{
    uint16_t *_soundBuffer;
    int videoWidth, videoHeight;
    NSString *romPath;
    NSString *romFile;
    NSString *autoLoadStatefileName;
    
    GLuint iFBO;
}
@end

__weak ReicastGameCore *_current;

//void dc_savestate(string fileName);
//void dc_loadstate(string fileName);

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

void common_linux_setup();
int dc_init(int argc,wchar* argv[]);
void dc_run();
void dc_term();
void dc_stop();

volatile bool has_init = false;
- (void)emuthread {
    settings.profile.run_counts = 0;
    common_linux_setup();
    
    // Set battery save dir
    NSURL *SavesDirectory = [NSURL fileURLWithPath:[self batterySavesDirectoryPath]];
    
    // create the data directory
   [[NSFileManager defaultManager] createDirectoryAtURL:[[NSURL fileURLWithPath:[self supportDirectoryPath]] URLByAppendingPathComponent:@"data"] withIntermediateDirectories:YES attributes:nil error:nil];
    
    //create the save direcory for the game
    [[NSFileManager defaultManager] createDirectoryAtURL:[SavesDirectory URLByAppendingPathComponent:romFile] withIntermediateDirectories:YES attributes:nil error:nil];
    
    set_user_config_dir([[self supportDirectoryPath] UTF8String]);
    set_user_data_dir([SavesDirectory URLByAppendingPathComponent:romFile].path.fileSystemRepresentation);
    add_system_data_dir([[self biosDirectoryPath] fileSystemRepresentation]);
    add_system_config_dir([[self biosDirectoryPath] fileSystemRepresentation]);
    
    char* argv[] = { "reicast", (char*)[romPath UTF8String] };
    dc_init(2,argv);

    has_init = true;
    
    dc_run();
}

- (BOOL)loadFileAtPath:(NSString *)path error:(NSError **)error
{
    romFile = [[path lastPathComponent] stringByDeletingPathExtension];

    printf(" OPENEMU romFile %s\n", [romFile UTF8String]);
    
    romPath = path;
    
    return YES;
}

- (void)setupEmulation
{
    screen_width = videoWidth;
    screen_height = videoHeight;
    RegisterAudioBackend(&audiobackend_openemu);
    
    // Set player to 4
    cfgSaveInt("players", "nb", 4);
    
    //Disable the OE framelimiting
    [self.renderDelegate suspendFPSLimiting];
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

        while (!has_init) { ; }
    }
    
    
    if (self.needsDoubleBufferedFBO) {
        [self.renderDelegate presentDoubleBufferedFBO];
    } else {
        glBindFramebuffer(GL_FRAMEBUFFER,(GLuint)[[self.renderDelegate presentationFramebuffer] integerValue]);
    }
    
    while (!rend_framePending()){;}

    while (!rend_single_frame()) {;};
    
    calcFPS();
}

# pragma mark - Video

extern int screen_width,screen_height;
bool rend_framePending();
bool rend_single_frame();
bool gles_init();
double emuFrameInterval = 60;

void calcFPS(){
    const int spg_clks[4] = { 26944080, 13458568, 13462800, 26944080 };
    u32 pixel_clock = spg_clks[(SPG_CONTROL.full >> 6) & 3];
    
    switch (pixel_clock)
    {
        case 26944080:
            emuFrameInterval = 60.00;
            //info->timing.fps = 60.00; /* (VGA  480 @ 60.00) */
            break;
        case 26917135:
            emuFrameInterval = 59.94;
            //info->timing.fps = 59.94; /* (NTSC 480 @ 59.94) */
            break;
        case 13462800:
            emuFrameInterval = 50.00;
            // info->timing.fps = 50.00; /* (PAL 240  @ 50.00) */
            break;
        case 13458568:
            emuFrameInterval = 59.94;
            // info->timing.fps = 59.94; /* (NTSC 240 @ 59.94) */
            break;
        case 25925600:
            emuFrameInterval = 50.00;
            //info->timing.fps = 50.00; /* (PAL 480  @ 50.00) */
            break;
    }
}
void os_SetWindowText(const char * text) {
    puts(text);
}

void os_DoEvents() {
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
    glFlush();
}

- (OEGameCoreRendering)gameCoreRendering
{
    return OEGameCoreRenderingOpenGL3Video;
}

- (BOOL)needsDoubleBufferedFBO
{
    return YES;
}

- (OEIntSize)bufferSize
{
    return OEIntSizeMake(videoWidth, videoHeight);
}

- (OEIntSize)aspectSize
{
    return OEIntSizeMake(4, 3);
}

- (NSTimeInterval)frameInterval
{
    return emuFrameInterval;
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

- (void)autoloadWaitThread
{
    @autoreleasepool
    {
        //Wait here until we get the signal for full initialization
        while (!has_init)
            usleep (10000);
        
        dc_loadstate(autoLoadStatefileName.fileSystemRepresentation, nil ,nil);
    }
}

static void _OESaveStateCallback(bool status, std::string message, void *cbUserData)
{
    void (^block)(BOOL, NSError *) = (__bridge_transfer void(^)(BOOL, NSError *))cbUserData;
    
    [_current endPausedExecution];
    
    block(status, nil);
}

- (void)saveStateToFileAtPath:(NSString *)fileName completionHandler:(void (^)(BOOL, NSError *))block
{
    if (has_init) {
        [self beginPausedExecution];
        dc_savestate(fileName.fileSystemRepresentation, _OESaveStateCallback, (__bridge_retained void *)[block copy]);
    }
}

- (void)loadStateFromFileAtPath:(NSString *)fileName completionHandler:(void (^)(BOOL, NSError *))block
{
    if (!has_init) {
        //Start a separate thread to load
        autoLoadStatefileName = fileName;
        
        [NSThread detachNewThreadSelector:@selector(autoloadWaitThread) toTarget:self withObject:nil];
        block(true, nil);
    } else {
        [self beginPausedExecution];
        dc_loadstate(fileName.fileSystemRepresentation, _OESaveStateCallback, (__bridge_retained void *)[block copy]);
    }
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

void os_SetupInput() {
//#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
  mcfg_CreateDevicesFromConfig();
//#endif
}

void UpdateInputState(u32 port) {
}

void UpdateVibration(u32 port, u32 value) {
}

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
    // FIXME: Dpad up/down seems to get set and released on the same frame, making it not do anything.
    // Need to ensure actions last across a frame?
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
