// =============================================================================
//  config.cpp  -  load tunables from PadOfTime.ini next to the DLL.
//  Uses GetPrivateProfile* so the ini format is the familiar [Section] key=value.
// =============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <string>
#include <cstdlib>
#include "config.h"
#include "log.h"

Config g_cfg;

extern HMODULE g_selfModule;   // set in dllmain

static std::string IniPath(const char* iniName)
{
    char path[MAX_PATH] = {};
    if (g_selfModule && GetModuleFileNameA(g_selfModule, path, MAX_PATH)) {
        std::string p(path);
        size_t slash = p.find_last_of("\\/");
        if (slash != std::string::npos)
            return p.substr(0, slash + 1) + iniName;
    }
    return iniName;
}

static float GetF(const char* sec, const char* key, float def, const char* ini)
{
    char buf[64], defbuf[64];
    snprintf(defbuf, sizeof(defbuf), "%f", def);
    GetPrivateProfileStringA(sec, key, defbuf, buf, sizeof(buf), ini);
    return (float)atof(buf);
}
static int GetI(const char* sec, const char* key, int def, const char* ini)
{
    return (int)GetPrivateProfileIntA(sec, key, def, ini);
}
static bool GetB(const char* sec, const char* key, bool def, const char* ini)
{
    return GetPrivateProfileIntA(sec, key, def ? 1 : 0, ini) != 0;
}

void Config_Load(const char* iniName)
{
    std::string ini = IniPath(iniName);
    if (GetFileAttributesA(ini.c_str()) == INVALID_FILE_ATTRIBUTES) {
        LOG("no ini at %s - using defaults", ini.c_str());
        return;
    }
    const char* p = ini.c_str();

    g_cfg.enableLog       = GetB("General", "EnableLog", g_cfg.enableLog, p);
    g_cfg.passthrough     = GetB("General", "Passthrough", g_cfg.passthrough, p);
    g_cfg.controllerIndex = GetI("General", "ControllerIndex", g_cfg.controllerIndex, p);
    g_cfg.allowRawFallback = GetB("General", "AllowRawFallback", g_cfg.allowRawFallback, p);

    g_cfg.cameraSensitivity = GetI("Sensitivity", "CameraSensitivity", g_cfg.cameraSensitivity, p);
    g_cfg.moveDeadzone      = GetF("Sensitivity", "MoveDeadzone",      g_cfg.moveDeadzone,      p);
    g_cfg.cameraDeadzone    = GetF("Sensitivity", "CameraDeadzone",    g_cfg.cameraDeadzone,    p);
    g_cfg.moveMaxRange      = GetI("Sensitivity", "MoveMaxStickRange", g_cfg.moveMaxRange,      p);
    g_cfg.cameraMaxRange    = GetI("Sensitivity", "CameraMaxStickRange", g_cfg.cameraMaxRange,  p);
    g_cfg.triggerThreshold  = GetF("Sensitivity", "TriggerThreshold",  g_cfg.triggerThreshold,  p);
    g_cfg.axisSnapRatio     = GetF("Sensitivity", "AxisSnapRatio",     g_cfg.axisSnapRatio,     p);

    g_cfg.invertMoveY   = GetB("Axes", "InvertMoveY",   g_cfg.invertMoveY,   p);
    g_cfg.invertCameraY = GetB("Axes", "InvertCameraY", g_cfg.invertCameraY, p);
    g_cfg.invertCameraX = GetB("Axes", "InvertCameraX", g_cfg.invertCameraX, p);
    g_cfg.swapTriggers  = GetB("Axes", "SwapTriggers",  g_cfg.swapTriggers,  p);
    g_cfg.cameraOnZRz   = GetB("Axes", "CameraOnZRz",   g_cfg.cameraOnZRz,   p);

    g_cfg.spoofVidPid = GetB("Spoof", "SpoofVidPid", g_cfg.spoofVidPid, p);
    {
        char buf[32];
        char defv[16]; snprintf(defv, sizeof(defv), "0x%04X", g_cfg.spoofVID);
        GetPrivateProfileStringA("Spoof", "VID", defv, buf, sizeof(buf), p);
        g_cfg.spoofVID = (int)strtol(buf, nullptr, 0);
        char defp[16]; snprintf(defp, sizeof(defp), "0x%04X", g_cfg.spoofPID);
        GetPrivateProfileStringA("Spoof", "PID", defp, buf, sizeof(buf), p);
        g_cfg.spoofPID = (int)strtol(buf, nullptr, 0);
    }

    g_cfg.btnA     = GetI("Buttons", "A",     g_cfg.btnA,     p);
    g_cfg.btnB     = GetI("Buttons", "B",     g_cfg.btnB,     p);
    g_cfg.btnX     = GetI("Buttons", "X",     g_cfg.btnX,     p);
    g_cfg.btnY     = GetI("Buttons", "Y",     g_cfg.btnY,     p);
    g_cfg.btnLB    = GetI("Buttons", "LB",    g_cfg.btnLB,    p);
    g_cfg.btnRB    = GetI("Buttons", "RB",    g_cfg.btnRB,    p);
    g_cfg.btnStart = GetI("Buttons", "Start", g_cfg.btnStart, p);
    g_cfg.btnBack  = GetI("Buttons", "Back",  g_cfg.btnBack,  p);
    g_cfg.btnLS    = GetI("Buttons", "LS",    g_cfg.btnLS,    p);
    g_cfg.btnRS    = GetI("Buttons", "RS",    g_cfg.btnRS,    p);

    LOG("ini loaded: moveDZ=%.2f camDZ=%.2f camSense=%d%% "
        "moveMax=%d%% camMax=%d%% invMoveY=%d invCamY=%d swapTrig=%d",
        g_cfg.moveDeadzone, g_cfg.cameraDeadzone, g_cfg.cameraSensitivity,
        g_cfg.moveMaxRange, g_cfg.cameraMaxRange, g_cfg.invertMoveY,
        g_cfg.invertCameraY, g_cfg.swapTriggers);
}

bool Proxy_LogEnabled() { return g_cfg.enableLog; }
bool Proxy_Passthrough() { return g_cfg.passthrough; }
