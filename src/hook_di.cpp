// =============================================================================
//  hook_di.cpp  -  vtable interception for IDirectInput8 / IDirectInputDevice8
//
//  Ported from Pad-Within (Warrior Within) to Prince of Persia: The Sands of
//  Time. Same strategy: don't create a separate COM object, patch specific
//  vtable slots on the REAL objects the system dinput8.dll already created.
//
//  Hooks on IDirectInput8:        CreateDevice        (vtable 3)
//                                 EnumDevices          (vtable 4)
//  Hooks on IDirectInputDevice8:  EnumObjects          (vtable 4, diag only)
//                                 GetProperty          (vtable 5, VIDPID spoof)
//                                 GetDeviceState       (vtable 9)
//                                 GetDeviceData        (vtable 10)
//                                 SetDataFormat        (vtable 11, diag)
//
//  Mouse and keyboard devices are created through the SAME underlying
//  dinput8.dll device class as the joystick (confirmed in Pad-Within: they
//  share one vtable), so we patch the vtable once per unique vtable pointer
//  and decide whether to actually synthesize INSIDE each hook, keyed by the
//  real device POINTER, not the vtable. This is exactly Pad-Within's design;
//  see IsTaggedJoystick/IsPrimaryJoystick below.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>

#include "proxy.h"
#include "log.h"
#include "config.h"

using CreateDevice_t   = HRESULT (STDMETHODCALLTYPE*)(IDirectInput8*, REFGUID, LPDIRECTINPUTDEVICE8*, LPUNKNOWN);
using EnumDevices_t    = HRESULT (STDMETHODCALLTYPE*)(IDirectInput8*, DWORD, LPDIENUMDEVICESCALLBACKA, LPVOID, DWORD);
using GetDeviceState_t = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8*, DWORD, LPVOID);
using GetDeviceData_t  = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8*, DWORD, LPDIDEVICEOBJECTDATA, LPDWORD, DWORD);
using SetDataFormat_t  = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8*, LPCDIDATAFORMAT);
using GetProperty_t    = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8*, REFGUID, LPDIPROPHEADER);
using EnumObjects_t    = HRESULT (STDMETHODCALLTYPE*)(IDirectInputDevice8*, LPDIENUMDEVICEOBJECTSCALLBACKA, LPVOID, DWORD);

static CreateDevice_t g_origCreateDevice = nullptr;
static EnumDevices_t  g_origEnumDevices  = nullptr;

struct DevHook {
    void**            vtbl;
    GetDeviceState_t  origState;
    GetDeviceData_t   origData;
    SetDataFormat_t   origSetFmt;
    GetProperty_t     origGetProp;
    EnumObjects_t     origEnum;
};
static DevHook g_devHooks[8] = {};
static int     g_devHookCount = 0;

// Tag which real device POINTERS are the joystick (vs mouse/keyboard, which
// may share the exact same vtable - see file header).
static IDirectInputDevice8* g_joyDevices[8] = {};
static int g_joyDeviceCount = 0;
static bool IsTaggedJoystick(IDirectInputDevice8* dev) {
    for (int i = 0; i < g_joyDeviceCount; ++i) if (g_joyDevices[i] == dev) return true;
    return false;
}
static bool IsPrimaryJoystick(IDirectInputDevice8* dev) {
    return g_joyDeviceCount > 0 && g_joyDevices[0] == dev;
}

static DevHook* FindHook(void** vtbl) {
    for (int i = 0; i < g_devHookCount; ++i) if (g_devHooks[i].vtbl == vtbl) return &g_devHooks[i];
    return nullptr;
}

static void* PatchVtblSlot(void** vtbl, int index, void* newFn)
{
    void* old = vtbl[index];
    DWORD oldProt;
    if (VirtualProtect(&vtbl[index], sizeof(void*), PAGE_EXECUTE_READWRITE, &oldProt)) {
        vtbl[index] = newFn;
        VirtualProtect(&vtbl[index], sizeof(void*), oldProt, &oldProt);
        return old;
    }
    LOG("VirtualProtect failed patching slot %d", index);
    return nullptr;
}

// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Hook_GetDeviceState(IDirectInputDevice8* self, DWORD cbData, LPVOID lpvData)
{
    void** vtbl = *reinterpret_cast<void***>(self);
    DevHook* h = FindHook(vtbl);
    HRESULT hr = (h && h->origState) ? h->origState(self, cbData, lpvData) : DIERR_NOTINITIALIZED;

    if (!IsPrimaryJoystick(self) || !lpvData) return hr;

    // Unconditional (not gated by EnableLog's throttle) - fires exactly once,
    // so we can always tell whether the game ever calls GetDeviceState on the
    // joystick at all in a given session, regardless of ini settings.
    static bool loggedFirstCall = false;
    if (!loggedFirstCall) {
        loggedFirstCall = true;
        LOG("Hook_GetDeviceState: FIRST call for primary joystick, cbData=%lu", (unsigned long)cbData);
    }

    extern void Proxy_LogJoystickCountOnce();
    Proxy_LogJoystickCountOnce();

    if (Proxy_Passthrough()) {
        if (Proxy_LogEnabled()) {
            static DWORD lastP = 0; DWORD now = GetTickCount();
            if (now - lastP > 400) {
                lastP = now;
                const long* a = reinterpret_cast<const long*>(lpvData);
                LOG("NATIVE %luB: X=%ld Y=%ld Z=%ld Rx=%ld Ry=%ld Rz=%ld S0=%ld S1=%ld POV0=%lu",
                    (unsigned long)cbData, a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                    *reinterpret_cast<const DWORD*>((const char*)lpvData + 32));
            }
        }
        return hr;   // do NOT synthesize
    }

    int buttonsCapacity = -1;
    if (cbData == sizeof(DIJOYSTATE))       buttonsCapacity = 32;
    else if (cbData == sizeof(DIJOYSTATE2)) buttonsCapacity = 128;

    if (buttonsCapacity < 0) {
        // Unknown/custom format - log once per size so we know what to add
        // support for, but don't touch a buffer we don't understand the
        // layout of.
        static DWORD lastSizes[8] = {}; static int nSizes = 0;
        bool seen = false;
        for (int i = 0; i < nSizes; ++i) if (lastSizes[i] == cbData) seen = true;
        if (!seen && nSizes < 8) { lastSizes[nSizes++] = cbData; LOG("GetDeviceState: unrecognized cbData=%lu, passing through unmodified", (unsigned long)cbData); }
        return hr;
    }

    ZeroMemory(lpvData, cbData);
    Proxy_FillJoyBuffer(lpvData, buttonsCapacity);
    return DI_OK;
}

// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Hook_GetDeviceData(
    IDirectInputDevice8* self, DWORD cbObjectData, LPDIDEVICEOBJECTDATA rgdod, LPDWORD pdwInOut, DWORD flags)
{
    void** vtbl = *reinterpret_cast<void***>(self);
    DevHook* h = FindHook(vtbl);
    HRESULT hr = (h && h->origData) ? h->origData(self, cbObjectData, rgdod, pdwInOut, flags) : DIERR_NOTINITIALIZED;

    // Buffered mode: we don't synthesize DIDEVICEOBJECTDATA events (would need
    // per-object dwOfs from SetDataFormat plus edge-detection against last
    // state). Same limitation as Pad-Within. Log so we notice if SoT actually
    // relies on this path for the pad instead of GetDeviceState.
    if (IsPrimaryJoystick(self) && Proxy_LogEnabled()) {
        static DWORD last = 0; DWORD now = GetTickCount();
        if (now - last > 1000) {
            last = now;
            LOG("GetDeviceData[JOY dev=%p]: hr=0x%08lX n=%lu (buffered mode not synthesized)",
                (void*)self, (unsigned long)hr, pdwInOut ? *pdwInOut : 0);
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
static const char* DofGuidName(const GUID* g)
{
    if (!g) return "any";
    if (IsEqualGUID(*g, GUID_XAxis))  return "X";
    if (IsEqualGUID(*g, GUID_YAxis))  return "Y";
    if (IsEqualGUID(*g, GUID_ZAxis))  return "Z";
    if (IsEqualGUID(*g, GUID_RxAxis)) return "Rx";
    if (IsEqualGUID(*g, GUID_RyAxis)) return "Ry";
    if (IsEqualGUID(*g, GUID_RzAxis)) return "Rz";
    if (IsEqualGUID(*g, GUID_Slider)) return "Slider";
    if (IsEqualGUID(*g, GUID_POV))    return "POV";
    if (IsEqualGUID(*g, GUID_Button)) return "Button";
    return "?";
}

static HRESULT STDMETHODCALLTYPE Hook_SetDataFormat(IDirectInputDevice8* self, LPCDIDATAFORMAT fmt)
{
    void** vtbl = *reinterpret_cast<void***>(self);
    DevHook* h = FindHook(vtbl);
    HRESULT hr = (h && h->origSetFmt) ? h->origSetFmt(self, fmt) : DIERR_NOTINITIALIZED;

    if (fmt && IsTaggedJoystick(self)) {
        LOG("SetDataFormat dev=%p: dwDataSize=%lu numObjs=%lu (DIJOYSTATE=%zu DIJOYSTATE2=%zu)",
            (void*)self, (unsigned long)fmt->dwDataSize, (unsigned long)fmt->dwNumObjs,
            sizeof(DIJOYSTATE), sizeof(DIJOYSTATE2));
        for (DWORD i = 0; i < fmt->dwNumObjs; ++i) {
            const DIOBJECTDATAFORMAT& o = fmt->rgodf[i];
            DWORD type = DIDFT_GETTYPE(o.dwType);
            const char* kind = (type & DIDFT_AXIS) ? "AXIS" : (type & DIDFT_BUTTON) ? "BUTTON" : (type & DIDFT_POV) ? "POV" : "?";
            LOG("   obj[%2lu] ofs=%3lu %-6s guid=%-6s inst=%lu",
                i, o.dwOfs, kind, DofGuidName(o.pguid), (unsigned long)DIDFT_GETINSTANCE(o.dwType));
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Diagnostic only (see file header): confirms the real device's NATIVE object
// layout before SetDataFormat remaps it. Doesn't drive GetDeviceState - that
// writes to the game-facing STANDARD offsets regardless (Pad-Within's own
// passthrough capture confirmed DirectInput does this remap for us).
static HRESULT STDMETHODCALLTYPE Hook_EnumObjects(
    IDirectInputDevice8* self, LPDIENUMDEVICEOBJECTSCALLBACKA cb, LPVOID ctx, DWORD flags)
{
    void** vtbl = *reinterpret_cast<void***>(self);
    DevHook* h = FindHook(vtbl);
    if (!h || !h->origEnum) return DIERR_NOTINITIALIZED;
    HRESULT hr = h->origEnum(self, cb, ctx, flags);
    if (IsTaggedJoystick(self) && Proxy_LogEnabled()) {
        LOG("EnumObjects dev=%p flags=0x%lX -> hr=0x%08lX", (void*)self, flags, (unsigned long)hr);
    }
    return hr;
}

// ---------------------------------------------------------------------------
static HRESULT STDMETHODCALLTYPE Hook_GetProperty(IDirectInputDevice8* self, REFGUID rguidProp, LPDIPROPHEADER pdiph)
{
    void** vtbl = *reinterpret_cast<void***>(self);
    DevHook* h = FindHook(vtbl);
    HRESULT hr = (h && h->origGetProp) ? h->origGetProp(self, rguidProp, pdiph) : DIERR_NOTINITIALIZED;

    if (g_cfg.spoofVidPid && IsTaggedJoystick(self)
        && &rguidProp == &DIPROP_VIDPID && pdiph && pdiph->dwSize >= sizeof(DIPROPDWORD)) {
        DIPROPDWORD* pd = reinterpret_cast<DIPROPDWORD*>(pdiph);
        DWORD oldVal = pd->dwData;
        WORD vid = (WORD)g_cfg.spoofVID;
        WORD pid = (WORD)(g_cfg.spoofPID ? g_cfg.spoofPID : HIWORD(oldVal));
        pd->dwData = MAKELONG(vid, pid);
        hr = DI_OK;
        LOG("GetProperty VIDPID spoof dev=%p: 0x%08lX -> 0x%08lX", (void*)self, oldVal, pd->dwData);
    }
    return hr;
}

// ---------------------------------------------------------------------------
static const char* GuidName(REFGUID g)
{
    if (IsEqualGUID(g, GUID_SysMouse))    return "SysMouse";
    if (IsEqualGUID(g, GUID_SysKeyboard)) return "SysKeyboard";
    return "other/joystick";
}
static bool IsJoystickGuid(REFGUID g) {
    return !IsEqualGUID(g, GUID_SysMouse) && !IsEqualGUID(g, GUID_SysKeyboard);
}

static HRESULT STDMETHODCALLTYPE Hook_CreateDevice(
    IDirectInput8* self, REFGUID rguid, LPDIRECTINPUTDEVICE8* out, LPUNKNOWN outer)
{
    HRESULT hr = g_origCreateDevice(self, rguid, out, outer);
    LOG("CreateDevice guid=%s -> hr=0x%08lX dev=%p", GuidName(rguid), (unsigned long)hr, (out ? *out : nullptr));

    if (SUCCEEDED(hr) && out && *out) {
        bool isJoy = IsJoystickGuid(rguid);
        if (isJoy && g_joyDeviceCount < 8) {
            g_joyDevices[g_joyDeviceCount++] = *out;
            LOG("  -> tagged joystick device %p (total joy devices=%d)", (void*)*out, g_joyDeviceCount);
        }
        void** devVtbl = *reinterpret_cast<void***>(*out);
        if (!FindHook(devVtbl) && g_devHookCount < 8) {
            GetDeviceState_t oS = reinterpret_cast<GetDeviceState_t>(
                PatchVtblSlot(devVtbl, 9,  reinterpret_cast<void*>(&Hook_GetDeviceState)));
            GetDeviceData_t  oD = reinterpret_cast<GetDeviceData_t>(
                PatchVtblSlot(devVtbl, 10, reinterpret_cast<void*>(&Hook_GetDeviceData)));
            SetDataFormat_t  oF = reinterpret_cast<SetDataFormat_t>(
                PatchVtblSlot(devVtbl, 11, reinterpret_cast<void*>(&Hook_SetDataFormat)));
            GetProperty_t    oP = reinterpret_cast<GetProperty_t>(
                PatchVtblSlot(devVtbl, 5,  reinterpret_cast<void*>(&Hook_GetProperty)));
            EnumObjects_t    oE = reinterpret_cast<EnumObjects_t>(
                PatchVtblSlot(devVtbl, 4,  reinterpret_cast<void*>(&Hook_EnumObjects)));
            g_devHooks[g_devHookCount++] = { devVtbl, oS, oD, oF, oP, oE };
            LOG("  patched vtable %p (hooks=%d)", (void*)devVtbl, g_devHookCount);
        }
    }
    return hr;
}

// ---------------------------------------------------------------------------
static LPDIENUMDEVICESCALLBACKA g_gameEnumDevCb = nullptr;
static LPVOID                   g_gameEnumDevRef = nullptr;

static BOOL CALLBACK EnumDevicesThunk(LPCDIDEVICEINSTANCEA inst, LPVOID ref)
{
    DIDEVICEINSTANCEA spoofed = *inst;
    BYTE devClass = (BYTE)GET_DIDEVICE_TYPE(inst->dwDevType);
    bool isJoy = (devClass != DI8DEVTYPE_KEYBOARD) && (devClass != DI8DEVTYPE_MOUSE);

    if (g_cfg.spoofVidPid && isJoy) {
        DWORD orig = *reinterpret_cast<DWORD*>(&spoofed.guidProduct);
        DWORD vidpid = MAKELONG((WORD)g_cfg.spoofVID, (WORD)g_cfg.spoofPID);
        *reinterpret_cast<DWORD*>(&spoofed.guidProduct) = vidpid;
        lstrcpynA(spoofed.tszProductName, "Controller (XBOX 360 For Windows)", sizeof(spoofed.tszProductName));
        LOG("EnumDevices spoof: '%s' guidProduct 0x%08lX -> 0x%08lX", inst->tszProductName, orig, vidpid);
    }
    return g_gameEnumDevCb ? g_gameEnumDevCb(&spoofed, g_gameEnumDevRef) : DIENUM_CONTINUE;
}

static HRESULT STDMETHODCALLTYPE Hook_EnumDevices(
    IDirectInput8* self, DWORD dwDevType, LPDIENUMDEVICESCALLBACKA cb, LPVOID ref, DWORD flags)
{
    if (!g_origEnumDevices) return DIERR_NOTINITIALIZED;
    g_gameEnumDevCb  = cb;
    g_gameEnumDevRef = ref;
    HRESULT hr = g_origEnumDevices(self, dwDevType, &EnumDevicesThunk, nullptr, flags);
    g_gameEnumDevCb = nullptr;
    return hr;
}

// ---------------------------------------------------------------------------
void Proxy_HookDirectInput8(void** ppvOut)
{
    IDirectInput8* di = reinterpret_cast<IDirectInput8*>(*ppvOut);
    void** vtbl = *reinterpret_cast<void***>(di);

    if (!g_origCreateDevice) {
        g_origCreateDevice = reinterpret_cast<CreateDevice_t>(
            PatchVtblSlot(vtbl, 3, reinterpret_cast<void*>(&Hook_CreateDevice)));
        g_origEnumDevices = reinterpret_cast<EnumDevices_t>(
            PatchVtblSlot(vtbl, 4, reinterpret_cast<void*>(&Hook_EnumDevices)));
        LOG("patched IDirectInput8 vtable %p, CreateDevice=%p EnumDevices=%p",
            (void*)vtbl, (void*)g_origCreateDevice, (void*)g_origEnumDevices);
    }
}
