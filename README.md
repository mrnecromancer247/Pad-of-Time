# Pad-of-Time

A `dinput8.dll` proxy that gives **Prince of Persia: The Sands of Time**
(Steam) a working modern gamepad, backed by SDL2.

Architecture ported from [Pad-Within](https://github.com/mrnecromancer247/Pad-Within)
(the Warrior Within version of this mod): instead of replacing DirectInput
devices with a custom COM object, we let the game create its own **real**
`IDirectInput8`/`IDirectInputDevice8` objects (via the real system
`dinput8.dll`, which we load and forward to), then vtable-patch a handful of
methods on those real objects in-place. Keyboard and mouse are never touched.

## Quick Start

Verified working on both the **Steam** and **GOG** releases. This proxy
never patches the game's own executable or hardcodes any addresses inside
it - it side-loads as `dinput8.dll` next to the exe and intercepts
DirectInput API calls, so it isn't tied to a specific build or storefront
in the first place. (In fact, the Steam and GOG executables tested here
turned out to be byte-identical - same size, same PE timestamp, same
imports - so there was nothing storefront-specific to account for.)

1. Download the latest Pad-of-Time release from GitHub.
2. Extract the files into the folder where the game's exe (`POP.exe`) lives.
3. Turn off Steam Input for this game (Steam Input remaps your controller
   before the game ever sees it, which interferes with this proxy).
4. Plug in your controller and launch the game. Open the in-game
   **Controls → Gamepad** settings and bind your controller using the
   layout below.

| Action           | Button / Axis                    |
|------------------|-----------------------------------|
| Move             | Left stick — X, Y axes            |
| Jump             | A / Cross                         |
| Special action   | RB / R1                           |
| Sword Attack     | X / Square                        |
| Use Dagger       | Y / Triangle                      |
| Cancel           | B / Circle                        |
| Rewind           | LB / L1                           |
| Camera Look      | RT / R2                           |
| Alternate View   | LT / L2                           |
| Reset Camera     | RS / R3                           |
| Camera           | Right stick — RX, RY axes         |

Unlike its sequel, this game doesn't let you navigate menus with a
controller, so use keyboard and mouse for menu navigation.

## Why this instead of patching the engine

The Steam exe already has a real DirectInput joystick path
(`PCDeviceJoystick`, `GUID_Joystick`, `gamepads.dat`) - it's just that modern
XInput-only pads either aren't visible to it as a legacy DirectInput
joystick, or the axes/deadzone feel bad. This proxy doesn't replace that
mechanism or touch the game's own executable; it makes sure the joystick
device the game creates reports the state we want.

## Included SDL2.dll

Ship the included `SDL2.dll` (**2.32.8.0**) alongside `dinput8.dll` - this
proxy is built and tested against it and **will not detect controllers
correctly with an older SDL2.dll** (earlier builds' Windows joystick
backends behave differently; see the HIDAPI notes below). If you rebuild
from source, keep using this version unless you've re-verified another one.

## Troubleshooting: pad shows up but nothing happens

If `PadOfTime.log` (`EnableLog=1`) shows `SDL_NumJoysticks()=1` but with a
generic name (`"HID-compliant game controller"`) instead of your controller's
real name, and `isGC=0` - the SDL2 HIDAPI backend never got to claim the
device. This is exactly what the bundled `SDL2.dll` (2.32.8.0) fixes - an
older SDL2 build is the most common cause. If you're already on the
bundled version and still hit this, check for background controller
software (Steam's controller support, DS4Windows, BetterJoy, Joy2Key,
vendor driver utilities) that might be holding the device's HID handle
exclusively, and consider `AllowRawFallback=1` in the ini as a last resort -
it reads the pad as a raw joystick with a blind numeric axis/button guess
instead of a real mapping, so expect to retune `[Buttons]` by watching the
`[raw]` log lines while pressing each button individually.

## Assumptions not verified on real hardware for every case

- `DIJOYSTATE` vs `DIJOYSTATE2`: both share an identical axis/POV layout in
  their first 36 bytes, differing only in button count (32 vs 128).
  `Hook_GetDeviceState` detects which one is in play from `cbData` at
  runtime and fills either one with the same code path. Confirmed via log
  on the Steam build: `dwDataSize=80` → `DIJOYSTATE`.
- Buffered input (`GetDeviceData`) is logged for diagnostics but not
  synthesized - if the game ever reads the pad that way instead of
  `GetDeviceState`, that path still needs filling in.

## PadOfTime.ini reference

All settings live next to `dinput8.dll`, in `PadOfTime.ini`. Every value has
a working default, so a missing file or a missing key just falls back to
it - you only need to add lines for what you actually want to change.

### `[General]`

| Key | Default | What it does |
|---|---|---|
| `EnableLog` | `0` | Write `PadOfTime.log` next to the game exe. Turn on only when troubleshooting - it logs every DirectInput call the game makes plus a live dump of the pad state. |
| `Passthrough` | `0` | Diagnostic mode: don't synthesize anything, just pass the real device's native state through untouched and log the raw buffer. Needs `EnableLog=1` too. Useful to check whether the game's own DirectInput handling is already usable before deciding anything needs fixing. |
| `ControllerIndex` | `-1` | Which controller to use when more than one is connected. `-1` = auto (picks whichever pad is actually sending input). Otherwise, the SDL device index (`0`, `1`, ...). |
| `AllowRawFallback` | `0` | Last-resort fallback: if a pad never gets a proper `SDL_GameController` mapping, read it as a raw joystick with a blind numeric axis/button guess instead. See the Troubleshooting section - this masks the real problem rather than fixing it, so try everything else first. |

### `[Sensitivity]`

| Key | Default | What it does |
|---|---|---|
| `MoveDeadzone` | `0.18` | Radial deadzone on the left stick (movement), `0.0`-`1.0` as a fraction of full stick travel. Raise it if the Prince drifts/creeps with the stick centered. |
| `MoveMaxStickRange` | `100` | Outer calibration, as a percent. If your stick is worn or loose and never *quite* reaches full physical deflection, lower this (e.g. `90`) so that 90% deflection already reads as 100%. `100` = off. |
| `CameraSensitivity` | `65` | Camera speed as a percent; `50` is the formula's baseline (1.0x). The shipped default (`65`) is a bit faster than baseline. No upper limit. |
| `CameraDeadzone` | `0.20` | Radial deadzone on the right stick (camera), same idea as `MoveDeadzone`. |
| `CameraMaxStickRange` | `100` | Outer calibration for the camera stick, same idea as `MoveMaxStickRange`. |
| `TriggerThreshold` | `0.20` | Reserved - only used if triggers ever get read as digital buttons rather than an analog axis. |
| `AxisSnapRatio` | `0.2` | Cross-axis suppression: if one axis' magnitude is below this fraction of the other's, snap it to zero. Helps the game's Controls binding screen read a clean single axis instead of latching onto stick noise on the "other" axis. Hall-effect sticks have almost no deadzone but do emit some noise at center - counterintuitively, a *higher* value here suppresses that better than a lower one. `0` disables it. |

### `[Axes]`

| Key | Default | What it does |
|---|---|---|
| `InvertMoveY` | `0` | Invert the left stick's vertical axis. |
| `InvertCameraY` | `0` | Invert the right stick's vertical axis (camera up/down). |
| `InvertCameraX` | `0` | Invert the right stick's horizontal axis (camera left/right). |
| `SwapTriggers` | `0` | `0`: RT drives Z(+), LT drives Z(-). `1`: swapped. |
| `CameraOnZRz` | `0` | `0` (default): camera reads on Rx/Ry, triggers read on Z. `1`: camera reads on Z/Rz, triggers read on Rx instead - try this if the camera stick behaves wrong. |

### `[Spoof]`

| Key | Default | What it does |
|---|---|---|
| `SpoofVidPid` | `0` | Present a consistent controller VID/PID to the game regardless of which real pad is plugged in, in case the game's own gamepad config keys anything off device identity. Off by default. |
| `VID` | `0x045e` | Vendor ID to spoof (default: Microsoft). Only used if `SpoofVidPid=1`. |
| `PID` | `0x0007` | Product ID to spoof. Only used if `SpoofVidPid=1`. |

### `[Buttons]`

`rgbButtons` index for each named SDL button. `-1` = unmapped. Defaults are
confirmed against the game's own Controls → Gamepad binding screen - see
the Quick Start table above for what each one does in-game.

| Key | Default | In-game action (see Quick Start table) |
|---|---|---|
| `A` | `0` | Jump |
| `B` | `1` | Cancel |
| `X` | `2` | Sword Attack |
| `Y` | `3` | Use Dagger |
| `LB` | `4` | Rewind |
| `RB` | `5` | Special action |
| `Back` | `6` | - |
| `Start` | `7` | - |
| `LS` | `8` | - |
| `RS` | `9` | Reset Camera |

## Building from source

**On Windows (primary path, matches Pad-Within):**
```
cmake -B build -A Win32 -DSDL2_DIR=<path-to-SDL2-cmake-config-from-vcpkg>
cmake --build build --config Release
```

**Linux/MinGW (for CI or sandbox verification only):**
```
SDL_ROOT=/path/to/mingw-sdl2 make
```
Either way, for the actual release you still want to ship the official
2.32.8.0 `SDL2.dll` alongside the built `dinput8.dll` - see above.

## Credits

Architecture and much of the vtable-hooking approach ported from
[Pad-Within](https://github.com/mrnecromancer247/Pad-Within) (Prince of
Persia: Warrior Within).
