// =============================================================================
//  PadOfTime  -  dinput8.dll proxy for Prince of Persia: The Sands of Time (Steam)
//
//  Same pattern as Pad-Within (Warrior Within): we ARE dinput8.dll (the exe's
//  only DirectInput import). We load the real system dinput8.dll ourselves
//  and forward DirectInput8Create there, then vtable-patch the REAL objects
//  it returns (CreateDevice/EnumDevices on IDirectInput8; GetDeviceState and
//  friends on IDirectInputDevice8) rather than creating our own COM object.
//  See hook_di.cpp for the actual interception and input_sdl.cpp for the
//  SDL2 fill logic.
//
//  SoT's own PCDeviceJoystick class (confirmed via static analysis of the
//  Steam exe: PCDeviceJoystick::Init/SetRange error strings, gamepads.dat)
//  already talks to a DirectInput joystick via GUID_Joystick - we don't
//  replace that mechanism, we just make sure the joystick device it creates
//  reports controller state the way we want.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>

#include "proxy.h"
#include "log.h"
#include "config.h"

HMODULE g_selfModule = nullptr;

static HMODULE g_realDInput = nullptr;
static HMODULE g_asiChain   = nullptr;   // optional widescreen/ASI loader chain
using DirectInput8Create_t = HRESULT (WINAPI*)(HINSTANCE, DWORD, REFIID, LPVOID*, LPUNKNOWN);
static DirectInput8Create_t g_realDI8Create = nullptr;

// ---------------------------------------------------------------------------
static bool LoadRealDInput()
{
    char sysdir[MAX_PATH];
    if (!GetSystemDirectoryA(sysdir, MAX_PATH)) return false;
    std::string path = std::string(sysdir) + "\\dinput8.dll";

    g_realDInput = LoadLibraryA(path.c_str());
    if (!g_realDInput) {
        LOG("FATAL: could not load real dinput8 at %s (err %lu)", path.c_str(), GetLastError());
        return false;
    }
    g_realDI8Create = reinterpret_cast<DirectInput8Create_t>(GetProcAddress(g_realDInput, "DirectInput8Create"));
    if (!g_realDI8Create) {
        LOG("FATAL: real dinput8 has no DirectInput8Create export");
        return false;
    }
    LOG("real dinput8 loaded @ %p, DI8Create @ %p", (void*)g_realDInput, (void*)g_realDI8Create);
    return true;
}

// Optional chain to a renamed third-party dinput8 (e.g. a widescreen ASI
// loader). We deliberately don't forward DirectInput8Create through it - we
// go straight to the system DLL - so our own behaviour stays independent of
// whatever that stub does.
static void ChainAsiLoader()
{
    g_asiChain = LoadLibraryA("d8hooked.dll");
    if (g_asiChain)
        LOG("chained ASI loader d8hooked.dll @ %p", (void*)g_asiChain);
    else
        LOG("no d8hooked.dll found (err %lu) - running without ASI chain, that's fine", GetLastError());
}

// ---------------------------------------------------------------------------
// All heavy initialization happens on first DirectInput8Create call, NOT in
// DllMain. DllMain runs under the Windows loader lock, where LoadLibrary or
// starting threads (SDL_Init with SDL_HINT_JOYSTICK_THREAD does) can deadlock
// the process before the game even creates its window.
// ---------------------------------------------------------------------------
static bool g_initDone = false;

static void EnsureInit()
{
    if (g_initDone) return;
    g_initDone = true;
    Config_Load("PadOfTime.ini");
    if (g_cfg.enableLog) Log_Init("PadOfTime.log");
    LOG("EnsureInit: deferred init (log enabled via ini)");
    LoadRealDInput();
    ChainAsiLoader();
    Proxy_InitInput();
}

extern "C" HRESULT WINAPI DirectInput8Create(
    HINSTANCE hinst, DWORD dwVersion, REFIID riidltf, LPVOID* ppvOut, LPUNKNOWN punkOuter)
{
    EnsureInit();

    if (!g_realDI8Create) {
        LOG("DirectInput8Create: real dinput8 unavailable, returning E_FAIL");
        return E_FAIL;
    }

    HRESULT hr = g_realDI8Create(hinst, dwVersion, riidltf, ppvOut, punkOuter);
    LOG("DirectInput8Create -> hr=0x%08lX, ppvOut=%p", hr, ppvOut ? *ppvOut : nullptr);

    if (SUCCEEDED(hr) && ppvOut && *ppvOut) {
        Proxy_HookDirectInput8(ppvOut);
    }
    return hr;
}

// ---------------------------------------------------------------------------
BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID)
{
    switch (reason) {
    case DLL_PROCESS_ATTACH:
        DisableThreadLibraryCalls(hModule);
        g_selfModule = hModule;
        break;
    case DLL_PROCESS_DETACH:
        Proxy_ShutdownInput();
        if (g_asiChain)   FreeLibrary(g_asiChain);
        if (g_realDInput) FreeLibrary(g_realDInput);
        LOG("=== detach ===");
        Log_Close();
        break;
    }
    return TRUE;
}
