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
