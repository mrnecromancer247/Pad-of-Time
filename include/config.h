#pragma once

// ---------------------------------------------------------------------------
// Runtime configuration loaded from PadOfTime.ini (next to the DLL).
// All fields have sane defaults so the mod works with no ini present.
// Structure deliberately mirrors Pad-Within's Config (Prince of Persia:
// Warrior Within) so the two mods are easy to maintain side by side.
// ---------------------------------------------------------------------------
struct Config
{
    // --- general ---
    bool enableLog = false;   // write PadOfTime.log (off by default)
    bool passthrough = false; // DIAG: don't synthesize; pass native dinput through
                              // and dump the raw buffer, to capture the real
                              // device's working native layout.

    // --- controller selection ---
    int  controllerIndex = -1;  // -1 = auto (pick the pad actually sending input)
    // Opt-in only: read an unmapped pad as a raw SDL_Joystick with a blind
    // numeric axis/button guess, if it never gets a proper GameController
    // mapping. Off by default - see README's hidapi troubleshooting section
    // before reaching for this; it's a guess, not a calibrated mapping.
    bool allowRawFallback = false;

    // --- sensitivity / feel ---
    int   cameraSensitivity = 65;    // percent; 50 = 1.0x multiplier (baseline)
    float moveDeadzone     = 0.18f;  // radial deadzone, left stick, 0..1
    float cameraDeadzone   = 0.20f;  // radial deadzone, right stick, 0..1
    int   moveMaxRange     = 100;    // outer calibration %, worn/loose sticks
    int   cameraMaxRange   = 100;
    float triggerThreshold = 0.20f;  // only used if triggers act as buttons
    // Cross-axis suppression, same idea as Pad-Within's AxisSnapRatio: if SoT's
    // Controls-menu axis-binding screen has the same "grabs the first axis
    // past a tiny threshold" bug that Warrior Within had, this is the knob to
    // fix it. Hall-effect sticks have almost no deadzone, but they still emit
    // a bit of low-level noise at center - too LOW a ratio doesn't suppress
    // that noise enough for the binding screen to read a clean single axis,
    // so a higher value than you'd expect works better here, not lower.
    float axisSnapRatio    = 0.2f;

    // --- axis inversion ---
    bool invertMoveY   = false;
    bool invertCameraY = false;
    bool invertCameraX = false;

    // --- trigger -> Z axis direction ---
    bool swapTriggers = false;   // false: RT -> Z(+), LT -> Z(-)

    // --- axis routing ---
    //   cameraOnZRz=false: right stick -> lRx/lRy, triggers -> lZ  (default)
    //   cameraOnZRz=true : right stick -> lZ/lRz,  triggers -> lRx
    bool cameraOnZRz = false;

    // --- VID/PID spoof: present a consistent controller identity to the game
    // regardless of which real pad is plugged in, in case SoT's Gamepads.DAT
    // keys anything off VID/PID the way WW's Profile.DAT does.
    bool spoofVidPid = false;
    int  spoofVID    = 0x045e;   // Microsoft
    int  spoofPID    = 0x0007;

    // --- button map: rgbButtons index for each SDL button. -1 = unmapped.
    // Confirmed against the game's own Controls -> Gamepad binding screen -
    // see README's Quick Start table for what each one does in-game (Jump,
    // Special Action, Sword Attack, Use Dagger, Cancel, Rewind, Reset Camera).
    int btnA  = 0;
    int btnB  = 1;
    int btnX  = 2;
    int btnY  = 3;
    int btnLB = 4;
    int btnRB = 5;
    int btnBack  = 6;
    int btnStart = 7;
    int btnLS = 8;
    int btnRS = 9;
};

// Global config instance.
extern Config g_cfg;

// Load PadOfTime.ini from the DLL's own directory. Missing file / keys keep
// defaults. Safe to call again to reload.
void Config_Load(const char* iniName);

bool Proxy_LogEnabled();
bool Proxy_Passthrough();
