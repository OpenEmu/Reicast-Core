// nullDC.cpp : Makes magic cookies
//

//initialse Emu
#include "types.h"
#include "oslib/oslib.h"
#include "oslib/audiostream.h"
#include "hw/mem/_vmem.h"
#include "stdclass.h"
#include "cfg/cfg.h"

#include "types.h"
#include "hw/maple/maple_cfg.h"
#include "hw/sh4/sh4_mem.h"

#include "webui/server.h"
#include "hw/naomi/naomi_cart.h"
#include "reios/reios.h"
#include "hw/sh4/sh4_sched.h"
#include "hw/pvr/Renderer_if.h"
#include "hw/pvr/spg.h"
#include "hw/aica/dsp.h"
#include "imgread/common.h"

void FlushCache();
void LoadCustom();
void dc_resume_emu(bool continue_running);

settings_t settings;
// Set if game has corresponding option by default, so that it's not saved in the config
static bool rtt_to_buffer_game;
static bool safemode_game;
static bool tr_poly_depth_mask_game;
static bool extra_depth_game;

static bool continue_running = false;
static cMutex mtx_mainloop ;
static cResetEvent resume_mainloop(false, true);

//OpenEmu:  SaveState Location name and Callback function
#include <functional>
string OEStateFilePath;
typedef std::function<void(bool status, const std::string &message, void *cbUserData)> Callback;
Callback OECallback;

void *OEUserData;
static void* dc_loadstate_thread(void* p);
static void* dc_savestate_thread(void* p);
extern char bios_dir[1024];

//  OpenEmu SaveState functions
void dc_savestate(const std::string &fileName, Callback callback, void *cbUserData)
{
    printf("Saving State: %s", fileName.c_str());
    
    OECallback = callback;
    OEUserData = cbUserData;
    OEStateFilePath = fileName;

    cThread thd(dc_savestate_thread,0);
    thd.Start() ;
}

void dc_loadstate(const std::string &fileName, Callback callback, void *cbUserData)
{
    printf ("Loading State: %s", fileName.c_str());
    
    OECallback = callback;
    OEUserData = cbUserData;
    OEStateFilePath = fileName;

    cThread thd(dc_loadstate_thread,0);
    thd.Start() ;
}

/*
 libndc
 
 //initialise (and parse the command line)
 ndc_init(argc,argv);
 
 ...
 //run a dreamcast slice
 //either a frame, or up to 25 ms of emulation
 //returns 1 if the frame is ready (fb needs to be flipped -- i'm looking at you android)
 ndc_step();
 
 ...
 //terminate (and free everything)
 ndc_term()
 */

#if HOST_OS==OS_WINDOWS
#include <windows.h>
#endif

/**
 * cpu_features_get_time_usec:
 *
 * Gets time in microseconds.
 *
 * Returns: time in microseconds.
 **/
int64_t get_time_usec(void)
{
#if HOST_OS==OS_WINDOWS
    static LARGE_INTEGER freq;
    LARGE_INTEGER count;
    
    /* Frequency is guaranteed to not change. */
    if (!freq.QuadPart && !QueryPerformanceFrequency(&freq))
        return 0;
    
    if (!QueryPerformanceCounter(&count))
        return 0;
    return count.QuadPart * 1000000 / freq.QuadPart;
#elif defined(_POSIX_MONOTONIC_CLOCK) || defined(__QNX__) || defined(ANDROID) || defined(__MACH__) || HOST_OS==OS_LINUX
    struct timespec tv = {0};
    if (clock_gettime(CLOCK_MONOTONIC, &tv) < 0)
        return 0;
    return tv.tv_sec * INT64_C(1000000) + (tv.tv_nsec + 500) / 1000;
#elif defined(EMSCRIPTEN)
    return emscripten_get_now() * 1000;
#elif defined(__mips__) || defined(DJGPP)
    struct timeval tv;
    gettimeofday(&tv,NULL);
    return (1000000 * tv.tv_sec + tv.tv_usec);
#else
#error "Your platform does not have a timer function implemented in cpu_features_get_time_usec(). Cannot continue."
#endif
}


int GetFile(char *szFileName, char *szParse /* = 0 */, u32 flags /* = 0 */)
{
    cfgLoadStr("config","image",szFileName,"null");
    if (strcmp(szFileName,"null")==0)
    {
#if HOST_OS==OS_WINDOWS
        OPENFILENAME ofn;
        ZeroMemory( &ofn , sizeof( ofn));
        ofn.lStructSize = sizeof ( ofn );
        ofn.hwndOwner = NULL  ;
        ofn.lpstrFile = szFileName ;
        ofn.lpstrFile[0] = '\0';
        ofn.nMaxFile = MAX_PATH;
        ofn.lpstrFilter = "All\0*.*\0\0";
        ofn.nFilterIndex =1;
        ofn.lpstrFileTitle = NULL ;
        ofn.nMaxFileTitle = 0 ;
        ofn.lpstrInitialDir=NULL ;
        ofn.Flags = OFN_PATHMUSTEXIST|OFN_FILEMUSTEXIST ;
        
        if (GetOpenFileNameA(&ofn))
        {
            //already there
            //strcpy(szFileName,ofn.lpstrFile);
        }
#endif
    }
    
    return 1;
}


s32 plugins_Init()
{
    
    if (s32 rv = libPvr_Init())
        return rv;
    
#ifndef TARGET_DISPFRAME
    if (s32 rv = libGDR_Init())
        return rv;
#endif
#if DC_PLATFORM == DC_PLATFORM_NAOMI || DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
    if (!naomi_cart_SelectFile(libPvr_GetRenderTarget()))
        return rv_serror;
#endif
    
    if (s32 rv = libAICA_Init())
        return rv;
    
    if (s32 rv = libARM_Init())
        return rv;
    
    //if (s32 rv = libExtDevice_Init())
    //    return rv;
    
    return rv_ok;
}

void plugins_Term()
{
    //term all plugins
    //libExtDevice_Term();
    libARM_Term();
    libAICA_Term();
    libGDR_Term();
    libPvr_Term();
}

void plugins_Reset(bool Manual)
{
    libPvr_Reset(Manual);
    libGDR_Reset(Manual);
    libAICA_Reset(Manual);
    libARM_Reset(Manual);
    //libExtDevice_Reset(Manual);
}

#if !defined(TARGET_NO_WEBUI) && !defined(TARGET_NO_THREADS)

void* webui_th(void* p)
{
    webui_start();
    return 0;
}

cThread webui_thd(&webui_th,0);
#endif

void LoadSpecialSettings()
{
#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
    printf("Game ID is [%s]\n", reios_product_number);
    rtt_to_buffer_game = false;
    safemode_game = false;
    tr_poly_depth_mask_game = false;
    extra_depth_game = false;
    
    // Tony Hawk's Pro Skater 2
    if (!strncmp("T13008D", reios_product_number, 7) || !strncmp("T13006N", reios_product_number, 7)
        // Tony Hawk's Pro Skater 1
        || !strncmp("T40205N", reios_product_number, 7)
        // Tony Hawk's Skateboarding
        || !strncmp("T40204D", reios_product_number, 7)
        // Skies of Arcadia
        || !strncmp("MK-51052", reios_product_number, 8))
    {
        settings.rend.RenderToTextureBuffer = 1;
        rtt_to_buffer_game = true;
    }
    if (!strncmp("HDR-0176", reios_product_number, 8) || !strncmp("RDC-0057", reios_product_number, 8))
    {
        // Cosmic Smash
        settings.rend.TranslucentPolygonDepthMask = 1;
        tr_poly_depth_mask_game = true;
    }
    // Pro Pinball Trilogy
    if (!strncmp("T30701D", reios_product_number, 7)
        // Demolition Racer
        || !strncmp("T15112N", reios_product_number, 7)
        // Star Wars - Episode I - Racer (United Kingdom)
        || !strncmp("T23001D", reios_product_number, 7)
        // Star Wars - Episode I - Racer (USA)
        || !strncmp("T23001N", reios_product_number, 7)
        // Record of Lodoss War (EU)
        || !strncmp("T7012D", reios_product_number, 6)
        // Record of Lodoss War (USA)
        || !strncmp("T40218N", reios_product_number, 7)
        // Surf Rocket Racers
        || !strncmp("T40216N", reios_product_number, 7))
    {
        printf("Enabling Dynarec safe mode for game %s\n", reios_product_number);
        settings.dynarec.safemode = 1;
        safemode_game = true;
    }
    // NHL 2K2
    if (!strncmp("MK-51182", reios_product_number, 8))
    {
        printf("Enabling Extra depth scaling for game %s\n", reios_product_number);
        settings.rend.ExtraDepthScale = 10000;
        extra_depth_game = true;
    }
#elif DC_PLATFORM == DC_PLATFORM_NAOMI || DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
    printf("Game ID is [%s]\n", naomi_game_id);
    
    if (!strcmp("METAL SLUG 6", naomi_game_id) || !strcmp("WAVE RUNNER GP", naomi_game_id))
    {
        printf("Enabling Dynarec safe mode for game %s\n", naomi_game_id);
        settings.dynarec.safemode = 1;
        safemode_game = true;
    }
    if (!strcmp("SAMURAI SPIRITS 6", naomi_game_id))
    {
        printf("Enabling Extra depth scaling for game %s\n", naomi_game_id);
        settings.rend.ExtraDepthScale = 1e26;
        extra_depth_game = true;
    }
    if (!strcmp("DYNAMIC GOLF", naomi_game_id)
        || !strcmp("SHOOTOUT POOL", naomi_game_id)
        || !strcmp("OUTTRIGGER     JAPAN", naomi_game_id)
        || !strcmp("CRACKIN'DJ  ver JAPAN", naomi_game_id)
        || !strcmp("CRACKIN'DJ PART2  ver JAPAN", naomi_game_id)
        || !strcmp("KICK '4' CASH", naomi_game_id))
    {
        printf("Enabling JVS rotary encoders for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 2;
    }
    else if (!strcmp("POWER STONE 2 JAPAN", naomi_game_id))
    {
        printf("Enabling 4-player setup for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 1;
    }
    else if (!strcmp("SEGA MARINE FISHING JAPAN", naomi_game_id))
    {
        printf("Enabling specific JVS setup for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 3;
    }
    else if (!strcmp("RINGOUT 4X4 JAPAN", naomi_game_id))
    {
        printf("Enabling specific JVS setup for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 4;
    }
    else if (!strcmp("NINJA ASSAULT", naomi_game_id))
    {
        printf("Enabling specific JVS setup for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 5;
    }
    else if (!strcmp(" BIOHAZARD  GUN SURVIVOR2", naomi_game_id))
    {
        printf("Enabling specific JVS setup for game %s\n", naomi_game_id);
        settings.input.JammaSetup = 7;
    }
    if (!strcmp("COSMIC SMASH IN JAPAN", naomi_game_id))
    {
        printf("Enabling translucent depth multipass for game %s\n", naomi_game_id);
        settings.rend.TranslucentPolygonDepthMask = true;
        tr_poly_depth_mask_game = true;
    }
#endif
}

void dc_reset()
{
    plugins_Reset(false);
    mem_Reset(false);
    
    sh4_cpu.Reset(false);
}

static bool init_done;

int dc_init(int argc,wchar* argv[])
{
    setbuf(stdin,0);
    setbuf(stdout,0);
    setbuf(stderr,0);
    if (init_done)
    {
        if(ParseCommandLine(argc,argv))
        {
            return 69;
        }
        InitSettings();
        LoadSettings(false);
        if (DiscSwap())
            LoadCustom();
        dc_reset();
        
        return 0;
    }
    if (!_vmem_reserve())
    {
        printf("Failed to alloc mem\n");
        return -1;
    }
    
#if !defined(TARGET_NO_WEBUI) && !defined(TARGET_NO_THREADS)
    webui_thd.Start();
#endif
    
    if(ParseCommandLine(argc,argv))
    {
        return 69;
    }
    if(!cfgOpen())
    {
        msgboxf("Unable to open config file",MBX_ICONERROR);
        return -4;
    }
    InitSettings();
    LoadSettings(false);
    
    os_CreateWindow();
    
#if HOST_OS != OS_DARWIN
#define DATA_PATH "/data/"
#else
#define DATA_PATH "/"
#endif
    
    //     OpenEmu:  Force to load from OE Bios Directory
        if (!LoadRomFiles(bios_dir))
        {
#ifdef USE_REIOS
        if (!LoadHle(get_readonly_data_path(DATA_PATH)))
        {
            return -5;
        }
        else
        {
            printf("Did not load bios, using reios\n");
        }
#else
        printf("Cannot find BIOS files\n");
        return -5;
#endif
    }
    
    if (plugins_Init())
    {
        return -3;
    }
    
    LoadCustom();
    
#if FEAT_SHREC != DYNAREC_NONE
    Get_Sh4Recompiler(&sh4_cpu);
    sh4_cpu.Init();        // Also initialize the interpreter
    if(settings.dynarec.Enable)
    {
        printf("Using Recompiler\n");
    }
    else
#endif
    {
        Get_Sh4Interpreter(&sh4_cpu);
#if FEAT_SHREC == DYNAREC_NONE
        sh4_cpu.Init();
#endif
        printf("Using Interpreter\n");
    }
    
    InitAudio();
    
    mem_Init();
    
    mem_map_default();
    
    os_SetupInput();
    
#if DC_PLATFORM == DC_PLATFORM_NAOMI
    mcfg_CreateNAOMIJamma();
#elif DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
    mcfg_CreateAtomisWaveControllers();
#endif
    init_done = true;
    
    dc_reset();
    
    return 0;
}

bool dc_is_running()
{
    return sh4_cpu.IsCpuRunning();
}

#ifndef TARGET_DISPFRAME
void dc_run()
{
    resume_mainloop.Set();
    
    while ( true )
    {
        bool dynarec_enabled = settings.dynarec.Enable;
        continue_running = false ;
        mtx_mainloop.Lock() ;
        sh4_cpu.Run();
        mtx_mainloop.Unlock() ;
        
#ifdef _WIN32
        // Avoid the looping audio when the emulator is paused
        TermAudio();
#endif
        while (!resume_mainloop.Wait(20))
            os_DoEvents();
        resume_mainloop.Set();
        
        if (dynarec_enabled != settings.dynarec.Enable)
        {
            if (settings.dynarec.Enable)
            {
                Get_Sh4Recompiler(&sh4_cpu);
                printf("Using Recompiler\n");
            }
            else
            {
                Get_Sh4Interpreter(&sh4_cpu);
                printf("Using Interpreter\n");
            }
            sh4_cpu.ResetCache();
        }
        if (!continue_running)
            break ;
#ifdef _WIN32
        InitAudio();
#endif
    }
}
#endif

void dc_term()
{
    printf ("Terminating DC Emu Thread");
    sh4_cpu.Term();
    plugins_Term();
    _vmem_release();
    
    mcfg_DestroyDevices();
    
    SaveSettings();
    SaveRomFiles(get_writable_data_path("/data/"));
    
    TermAudio();
    
#if !defined(TARGET_NO_WEBUI) && !defined(TARGET_NO_THREADS)
    extern void sighandler(int sig);
    sighandler(0);
    webui_thd.WaitToEnd();
#endif
    
}

#if defined(_ANDROID)
void dc_pause()
{
    SaveRomFiles(get_writable_data_path("/data/"));
}
#endif

void dc_stop()
{
    if (sh4_cpu.IsCpuRunning())
        sh4_cpu.Stop();
    else
        dc_resume_emu(false);
}

void dc_start()
{
    sh4_cpu.Start();
}

void InitSettings()
{
    settings.dreamcast.RTC            = GetRTC_now();
    settings.dynarec.Enable            = true;
    settings.dynarec.idleskip        = true;
    settings.dynarec.unstable_opt    = false;
    settings.dynarec.safemode        = true;
    settings.dreamcast.cable        = 3;    // TV composite
    settings.dreamcast.region        = 3;    // default
    settings.dreamcast.broadcast    = 4;    // default
    settings.dreamcast.language     = 6;    // default
    settings.aica.LimitFPS            = true;
    settings.aica.NoBatch            = false;    // This also controls the DSP. Disabled by default
    settings.aica.NoSound            = false;
    settings.rend.UseMipmaps        = true;
    settings.rend.WideScreen        = false;
    settings.rend.ShowFPS            = false;
    settings.rend.RenderToTextureBuffer = false;
    settings.rend.RenderToTextureUpscale = 1;
    settings.rend.TranslucentPolygonDepthMask = false;
    settings.rend.ModifierVolumes    = true;
    settings.rend.Clipping            = true;
    settings.rend.TextureUpscale    = 1;
    settings.rend.MaxFilteredTextureSize = 256;
    settings.rend.ExtraDepthScale   = 1.f;
    settings.rend.CustomTextures    = false;
    settings.rend.DumpTextures      = false;
    settings.rend.ScreenScaling     = 100;
    
    settings.pvr.ta_skip            = 0;
    settings.pvr.rend                = 0;
    
    settings.pvr.MaxThreads            = 3;
    settings.pvr.SynchronousRender    = true;
    
    settings.debug.SerialConsole    = false;
    
    settings.bios.UseReios            = 0;
    settings.reios.ElfFile            = "";
    
    settings.validate.OpenGlChecks  = false;
    
    settings.input.MouseSensitivity = 100;
    settings.input.JammaSetup = 0;
    for (int i = 0; i < MAPLE_PORTS; i++)
    {
        settings.input.maple_devices[i] = i == 0 ? MDT_SegaController : MDT_None;
        settings.input.maple_expansion_devices[i][0] = i == 0 ? MDT_SegaVMU : MDT_None;
        settings.input.maple_expansion_devices[i][1] = i == 0 ? MDT_SegaVMU : MDT_None;
    }
    
#if SUPPORT_DISPMANX
    settings.dispmanx.Width        = 640;
    settings.dispmanx.Height    = 480;
    settings.dispmanx.Keep_Aspect = true;
#endif
    
#if (HOST_OS != OS_LINUX || defined(_ANDROID) || defined(TARGET_PANDORA))
    settings.aica.BufferSize = 2048;
#else
    settings.aica.BufferSize = 1024;
#endif
    
#if USE_OMX
    settings.omx.Audio_Latency    = 100;
    settings.omx.Audio_HDMI        = true;
#endif
}

void LoadSettings(bool game_specific)
{
    const char *config_section = game_specific ? cfgGetGameId() : "config";
    const char *input_section = game_specific ? cfgGetGameId() : "input";
    
    settings.dynarec.Enable            = cfgLoadBool(config_section, "Dynarec.Enabled", settings.dynarec.Enable);
    settings.dynarec.idleskip        = cfgLoadBool(config_section, "Dynarec.idleskip", settings.dynarec.idleskip);
    settings.dynarec.unstable_opt    = cfgLoadBool(config_section, "Dynarec.unstable-opt", settings.dynarec.unstable_opt);
    settings.dynarec.safemode        = cfgLoadBool(config_section, "Dynarec.safe-mode", settings.dynarec.safemode);
    //disable_nvmem can't be loaded, because nvmem init is before cfg load
    settings.dreamcast.cable        = cfgLoadInt(config_section, "Dreamcast.Cable", settings.dreamcast.cable);
    settings.dreamcast.region        = cfgLoadInt(config_section, "Dreamcast.Region", settings.dreamcast.region);
    settings.dreamcast.broadcast    = cfgLoadInt(config_section, "Dreamcast.Broadcast", settings.dreamcast.broadcast);
    settings.dreamcast.language     = cfgLoadInt(config_section, "Dreamcast.Language", settings.dreamcast.language);
    settings.aica.LimitFPS            = cfgLoadBool(config_section, "aica.LimitFPS", settings.aica.LimitFPS);
    settings.aica.NoBatch            = cfgLoadBool(config_section, "aica.NoBatch", settings.aica.NoBatch);
    settings.aica.NoSound            = cfgLoadBool(config_section, "aica.NoSound", settings.aica.NoSound);
    settings.rend.UseMipmaps        = cfgLoadBool(config_section, "rend.UseMipmaps", settings.rend.UseMipmaps);
    settings.rend.WideScreen        = cfgLoadBool(config_section, "rend.WideScreen", settings.rend.WideScreen);
    settings.rend.ShowFPS            = cfgLoadBool(config_section, "rend.ShowFPS", settings.rend.ShowFPS);
    settings.rend.RenderToTextureBuffer = cfgLoadBool(config_section, "rend.RenderToTextureBuffer", settings.rend.RenderToTextureBuffer);
    settings.rend.RenderToTextureUpscale = cfgLoadInt(config_section, "rend.RenderToTextureUpscale", settings.rend.RenderToTextureUpscale);
    settings.rend.TranslucentPolygonDepthMask = cfgLoadBool(config_section, "rend.TranslucentPolygonDepthMask", settings.rend.TranslucentPolygonDepthMask);
    settings.rend.ModifierVolumes    = cfgLoadBool(config_section, "rend.ModifierVolumes", settings.rend.ModifierVolumes);
    settings.rend.Clipping            = cfgLoadBool(config_section, "rend.Clipping", settings.rend.Clipping);
    settings.rend.TextureUpscale    = cfgLoadInt(config_section, "rend.TextureUpscale", settings.rend.TextureUpscale);
    settings.rend.MaxFilteredTextureSize = cfgLoadInt(config_section,"rend.MaxFilteredTextureSize", settings.rend.MaxFilteredTextureSize);
    std::string extra_depth_scale_str = cfgLoadStr(config_section,"rend.ExtraDepthScale", "");
    if (!extra_depth_scale_str.empty())
    {
        settings.rend.ExtraDepthScale = atof(extra_depth_scale_str.c_str());
        if (settings.rend.ExtraDepthScale == 0)
            settings.rend.ExtraDepthScale = 1.f;
    }
    settings.rend.CustomTextures    = cfgLoadBool(config_section, "rend.CustomTextures", settings.rend.CustomTextures);
    settings.rend.DumpTextures      = cfgLoadBool(config_section, "rend.DumpTextures", settings.rend.DumpTextures);
    settings.rend.ScreenScaling     = cfgLoadInt(config_section, "rend.ScreenScaling", settings.rend.ScreenScaling);
    settings.rend.ScreenScaling = min(max(1, settings.rend.ScreenScaling), 100);
    
    settings.pvr.ta_skip            = cfgLoadInt(config_section, "ta.skip", settings.pvr.ta_skip);
    settings.pvr.rend                = cfgLoadInt(config_section, "pvr.rend", settings.pvr.rend);
    
    settings.pvr.MaxThreads            = cfgLoadInt(config_section, "pvr.MaxThreads", settings.pvr.MaxThreads);
    settings.pvr.SynchronousRender    = cfgLoadBool(config_section, "pvr.SynchronousRendering", settings.pvr.SynchronousRender);
    
    settings.debug.SerialConsole    = cfgLoadBool(config_section, "Debug.SerialConsoleEnabled", settings.debug.SerialConsole);
    
    settings.bios.UseReios            = cfgLoadBool(config_section, "bios.UseReios", settings.bios.UseReios);
    settings.reios.ElfFile            = cfgLoadStr(game_specific ? cfgGetGameId() : "reios", "ElfFile", settings.reios.ElfFile.c_str());
    
    settings.validate.OpenGlChecks  = cfgLoadBool(game_specific ? cfgGetGameId() : "validate", "OpenGlChecks", settings.validate.OpenGlChecks);
    
    settings.input.MouseSensitivity = cfgLoadInt(input_section, "MouseSensitivity", settings.input.MouseSensitivity);
    settings.input.JammaSetup = cfgLoadInt(input_section, "JammaSetup", settings.input.JammaSetup);
    for (int i = 0; i < MAPLE_PORTS; i++)
    {
        char device_name[32];
        sprintf(device_name, "device%d", i + 1);
        settings.input.maple_devices[i] = (MapleDeviceType)cfgLoadInt(input_section, device_name, settings.input.maple_devices[i]);
        sprintf(device_name, "device%d.1", i + 1);
        settings.input.maple_expansion_devices[i][0] = (MapleDeviceType)cfgLoadInt(input_section, device_name, settings.input.maple_expansion_devices[i][0]);
        sprintf(device_name, "device%d.2", i + 1);
        settings.input.maple_expansion_devices[i][1] = (MapleDeviceType)cfgLoadInt(input_section, device_name, settings.input.maple_expansion_devices[i][1]);
    }
    
#if SUPPORT_DISPMANX
    settings.dispmanx.Width        = cfgLoadInt(game_specific ? cfgGetGameId() : "dispmanx", "width", settings.dispmanx.Width);
    settings.dispmanx.Height    = cfgLoadInt(game_specific ? cfgGetGameId() : "dispmanx", "height", settings.dispmanx.Height);
    settings.dispmanx.Keep_Aspect    = cfgLoadBool(game_specific ? cfgGetGameId() : "dispmanx", "maintain_aspect", settings.dispmanx.Keep_Aspect);
#endif
    
#if (HOST_OS != OS_LINUX || defined(_ANDROID) || defined(TARGET_PANDORA))
    settings.aica.BufferSize=2048;
#else
    settings.aica.BufferSize=1024;
#endif
    
#if USE_OMX
    settings.omx.Audio_Latency    = cfgLoadInt(game_specific ? cfgGetGameId() : "omx", "audio_latency", settings.omx.Audio_Latency);
    settings.omx.Audio_HDMI        = cfgLoadBool(game_specific ? cfgGetGameId() : "omx", "audio_hdmi", settings.omx.Audio_HDMI);
#endif
    
    /*
     //make sure values are valid
     settings.dreamcast.cable    = min(max(settings.dreamcast.cable,    0),3);
     settings.dreamcast.region    = min(max(settings.dreamcast.region,   0),3);
     settings.dreamcast.broadcast= min(max(settings.dreamcast.broadcast,0),4);
     */
}

void LoadCustom()
{
#if DC_PLATFORM == DC_PLATFORM_DREAMCAST
    char *reios_id = reios_disk_id();
    
    char *p = reios_id + strlen(reios_id) - 1;
    while (p >= reios_id && *p == ' ')
        *p-- = '\0';
    if (*p == '\0')
        return;
#elif DC_PLATFORM == DC_PLATFORM_NAOMI || DC_PLATFORM == DC_PLATFORM_ATOMISWAVE
    char *reios_id = naomi_game_id;
    char *reios_software_name = naomi_game_id;
#endif
    
    // Default per-game settings
    LoadSpecialSettings();
    
    cfgSetGameId(reios_id);
    
    // Reload per-game settings
    LoadSettings(true);
}

void SaveSettings()
{
    cfgSaveBool("config", "Dynarec.Enabled", settings.dynarec.Enable);
    cfgSaveInt("config", "Dreamcast.Cable", settings.dreamcast.cable);
    cfgSaveInt("config", "Dreamcast.Region", settings.dreamcast.region);
    cfgSaveInt("config", "Dreamcast.Broadcast", settings.dreamcast.broadcast);
    cfgSaveBool("config", "Dynarec.idleskip", settings.dynarec.idleskip);
    cfgSaveBool("config", "Dynarec.unstable-opt", settings.dynarec.unstable_opt);
    if (!safemode_game || !settings.dynarec.safemode)
        cfgSaveBool("config", "Dynarec.safe-mode", settings.dynarec.safemode);
    cfgSaveInt("config", "Dreamcast.Language", settings.dreamcast.language);
    cfgSaveBool("config", "aica.LimitFPS", settings.aica.LimitFPS);
    cfgSaveBool("config", "aica.NoBatch", settings.aica.NoBatch);
    cfgSaveBool("config", "rend.WideScreen", settings.rend.WideScreen);
    cfgSaveBool("config", "rend.ShowFPS", settings.rend.ShowFPS);
    if (!rtt_to_buffer_game || !settings.rend.RenderToTextureBuffer)
        cfgSaveBool("config", "rend.RenderToTextureBuffer", settings.rend.RenderToTextureBuffer);
    cfgSaveInt("config", "rend.RenderToTextureUpscale", settings.rend.RenderToTextureUpscale);
    cfgSaveBool("config", "rend.ModifierVolumes", settings.rend.ModifierVolumes);
    cfgSaveBool("config", "rend.Clipping", settings.rend.Clipping);
    cfgSaveInt("config", "rend.TextureUpscale", settings.rend.TextureUpscale);
    cfgSaveInt("config", "rend.MaxFilteredTextureSize", settings.rend.MaxFilteredTextureSize);
    cfgSaveBool("config", "rend.CustomTextures", settings.rend.CustomTextures);
    cfgSaveBool("config", "rend.DumpTextures", settings.rend.DumpTextures);
    cfgSaveInt("config", "rend.ScreenScaling", settings.rend.ScreenScaling);
    cfgSaveInt("config", "ta.skip", settings.pvr.ta_skip);
    cfgSaveInt("config", "pvr.rend", settings.pvr.rend);
    
    cfgSaveInt("config", "pvr.MaxThreads", settings.pvr.MaxThreads);
    cfgSaveBool("config", "pvr.SynchronousRendering", settings.pvr.SynchronousRender);
    
    cfgSaveBool("config", "Debug.SerialConsoleEnabled", settings.debug.SerialConsole);
    cfgSaveInt("input", "MouseSensitivity", settings.input.MouseSensitivity);
    for (int i = 0; i < MAPLE_PORTS; i++)
    {
        char device_name[32];
        sprintf(device_name, "device%d", i + 1);
        cfgSaveInt("input", device_name, (s32)settings.input.maple_devices[i]);
        sprintf(device_name, "device%d.1", i + 1);
        cfgSaveInt("input", device_name, (s32)settings.input.maple_expansion_devices[i][0]);
        sprintf(device_name, "device%d.2", i + 1);
        cfgSaveInt("input", device_name, (s32)settings.input.maple_expansion_devices[i][1]);
    }
    
}

static bool wait_until_dc_running()
{
    int64_t start_time = get_time_usec() ;
    const int64_t FIVE_SECONDS = 5*1000000 ;
    while(!dc_is_running())
    {
        if ( start_time+FIVE_SECONDS < get_time_usec() )
        {
            //timeout elapsed - dc not getting a chance to run - just bail
            return false ;
        }
    }
    return true ;
}

static bool acquire_mainloop_lock()
{
    bool result = false ;
    int64_t start_time = get_time_usec() ;
    const int64_t FIVE_SECONDS = 5*1000000 ;
    
    while ( ( start_time+FIVE_SECONDS > get_time_usec() ) && !(result = mtx_mainloop.TryLock())  )
    {
        rend_cancel_emu_wait() ;
    }
    
    return result ;
}

bool dc_pause_emu()
{
    if (sh4_cpu.IsCpuRunning())
    {
#ifndef TARGET_NO_THREADS
        if (!wait_until_dc_running()) {
            printf("Can't open settings - dc loop kept running\n");
            return false;
        }
        resume_mainloop.Reset();
        
        dc_stop();
        
        if (!acquire_mainloop_lock())
        {
            printf("Can't open settings - could not acquire main loop lock\n");
            continue_running = true;
            resume_mainloop.Set();
            return false;
        }
#else
        dc_stop();
#endif
    }
    return true;
}

void dc_resume_emu(bool continue_running)
{
    if (!sh4_cpu.IsCpuRunning())
    {
        ::continue_running = continue_running;
        rend_cancel_emu_wait();
        resume_mainloop.Set();
        mtx_mainloop.Unlock();
    }
}

static void cleanup_serialize(void *data)
{
    if ( data != NULL )
        free(data) ;
    
    dc_resume_emu(true);
}

static string get_savestate_file_path()
{
    //OpenEmu:
    return OEStateFilePath;
}

static void* dc_savestate_thread(void* p)
{
    string filename;
    unsigned int total_size = 0 ;
    void *data = NULL ;
    void *data_ptr = NULL ;
    FILE *f ;
    
    if (!dc_pause_emu())
        return NULL;
    
    if ( ! dc_serialize(&data, &total_size) )
    {
        printf("Failed to save state - could not initialize total size\n") ;
        if (OECallback) OECallback(false, "Failed to save state - could not initialize total size.", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    data = malloc(total_size) ;
    if ( data == NULL )
    {
        printf("Failed to save state - could not malloc %d bytes", total_size) ;
        if (OECallback) OECallback(false, "Failed to load state - could not allocate Memory", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    data_ptr = data ;
    
    if ( ! dc_serialize(&data_ptr, &total_size) )
    {
        printf("Failed to save state - could not serialize data\n") ;
        if (OECallback) OECallback(false, "Failed to load state - could not serialize data", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    filename = get_savestate_file_path();
    f = fopen(filename.c_str(), "wb") ;
    
    if ( f == NULL )
    {
        printf("Failed to save state - could not open %s for writing\n", filename.c_str()) ;
        if (OECallback) OECallback(false, "Failed to load state - could not open file for writing", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    fwrite(data, 1, total_size, f) ;
    fclose(f);
    
    if (OECallback) OECallback(true, "Successfully Saved State", OEUserData);
    printf("Saved state to %s\n size %d", filename.c_str(), total_size) ;
    cleanup_serialize(data) ;
    
    return NULL;
}

static void* dc_loadstate_thread(void* p)
{
    string filename;
    unsigned int total_size = 0 ;
    void *data = NULL ;
    void *data_ptr = NULL ;
    FILE *f ;
    
    if (!dc_pause_emu())
        return NULL;
    
    filename = get_savestate_file_path();
    f = fopen(filename.c_str(), "rb") ;
    
    if ( f == NULL )
    {
        printf("Failed to load state - could not open %s for reading\n", filename.c_str()) ;
        if (OECallback) OECallback(false, "Failed to load state - could not open file for readming", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    total_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    data = malloc(total_size) ;
    if ( data == NULL )
    {
        printf("Failed to load state - could not malloc %d bytes", total_size) ;
        if (OECallback) OECallback(false, "Failed to load state - could not allocate Memory", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    fread(data, 1, total_size, f) ;
    fclose(f);
    
    
    data_ptr = data ;
    
    sh4_cpu.ResetCache();
#if FEAT_AREC == DYNAREC_JIT
    FlushCache();
#endif
    
    if ( ! dc_unserialize(&data_ptr, &total_size) )
    {
        printf("Failed to load state - could not unserialize data\n") ;
        if (OECallback) OECallback(false, "Failed to load state - could not unserialize data", OEUserData);
        cleanup_serialize(data) ;
        return NULL;
    }
    
    dsp.dyndirty = true;
    sh4_sched_ffts();
    CalculateSync();
    
    printf("Loaded state from %s size %d\n", filename.c_str(), total_size) ;
   
    cleanup_serialize(data) ;
    
    if (OECallback) OECallback(true, "Loaded state from file", OEUserData);
    
    return NULL;
}


void dc_savestate()
{
    cThread thd(dc_savestate_thread,0);
    thd.Start() ;
}

void dc_loadstate()
{
    cThread thd(dc_loadstate_thread,0);
    thd.Start() ;
}
