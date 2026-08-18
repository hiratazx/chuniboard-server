# Chuniboard-Chunithm-Server

A Windows server that bridges the **Chuniboard Android app** to a local Chunithm install via segatools, with optional Yuancon keyboard output for simulators.

## How it works

```
Chuniboard Android App
        │
        │  UDP (default) or TCP  port 52468
        ▼
chunithm_server.exe
        │
        ├──► BROKENITHM_SHARED_BUFFER (shared memory)
        │           │
        │           ▼
        │    chunihook.dll  (segatools path32=)
        │           │
        │           ▼
        │    chusanApp.exe  ← game reads slider + air from here
        │
        └──► SendInput  (Yuancon keyboard layout, optional -k flag)
                    │
                    ▼
             Simulators (SUSPlayer, Seaurchin, etc.)
```

## Compatible clients

**Yes, the existing Chuniboard Android app works as-is.** It uses the standard Brokenithm protocol (UDP/TCP packets) — the same protocol this server speaks. Just point it at this machine's IP on port 52468.

## Build

Requires CMake 3.21+, Ninja, and MSVC Build Tools (x86 target).

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Output:
- `build\chunithm_server.exe`
- `build\segatools\chuniio\chunihook.dll`

## Setup with Chunithm (segatools)

### 1. Copy the files

Copy both `chunithm_server.exe` and `chunihook.dll` into your Chunithm `bin\` folder (same folder as `chusanApp.exe` and `segatools.ini`).

### 2. Edit segatools.ini

Open `segatools.ini` and find the `[chuniio]` section:

```ini
[chuniio]
; Uncomment this if you have custom chuniio implementation comprised of a single 32bit DLL.
; (will use chu2to3 engine internally)
;path=
```

Change it to:

```ini
[chuniio]
path32=chunihook.dll
```

That's the only change needed. Everything else in `segatools.ini` stays the same:

| Section | What happens |
|---|---|
| `[slider]` | Leave commented — chunihook.dll provides its own slider thread |
| `[ir]` / `[io3] ir=` | Stays as fallback if Android doesn't send air data |
| `[io3] test=`, `service=`, `coin=` | Still work as keyboard fallbacks alongside the app |
| `[aimeio]` | Unchanged — built-in Aime emulation still applies |

### 3. Run

Run `chunithm_server.exe` **before or alongside** the game:

```
chunithm_server.exe [options]

  -p PORT   Listen port (default: 52468)
  -T        TCP mode instead of UDP
  -k        Also emit Yuancon keyboard events (for simulators)
```

Then launch the game normally via `launch.bat`.

### 4. Connect the app

Open the Chuniboard Android app and connect to this machine's local IP address on port **52468**.

## Air sensors

Air sensors are sent directly by the Android app and written to shared memory. The game reads all 6 air beam states through `chunihook.dll`. If the app doesn't send air data, the `[io3] ir=` key from `segatools.ini` is used as a fallback.

## Keyboard mode (`-k`)

When the `-k` flag is used, the server additionally emits `SendInput` keyboard events using the **Yuancon layout**:

| Input | Keys |
|---|---|
| Slider cells 1–32 (right → left) | `6 5 4 3 2 1 Z Y X W V U T S R Q P O N M L K J I H G F E D C B A` |
| Air sensors 1–6 (low → high) | `- = [ ] \ ;` |

This is for simulators that read keyboard input natively and is independent of `segatools.ini` key bindings.

## Credits

- [Brokenithm-Evolved](https://github.com/esterTion/Brokenithm-Evolved) — packet protocol and shared memory design
- [brokenithm-kb](https://github.com/4yn/brokenithm-kb) — keyboard emulation reference
- [segatools](https://gitea.tendokyu.moe/Dniel97/segatools) — chuniio API
