# PHASE

<p align="center"> <a href="https://koh-gt.github.io/phase-rhythm-shooter/web/phase.html"><img src="https://img.shields.io/badge/%E2%96%B6%20PLAY%20NOW-in%20your%20browser-ff4fd8?style=for-the-badge&labelColor=07020f" alt="Play now in your browser" height="40"></a> </p>
<p align="center"> <a href="https://koh-gt.github.io/phase-rhythm-shooter/web/phase.html"><img src="https://img.shields.io/badge/%E2%96%B6%20DEBUG%20VER-Unlimited%20XP-ff4fd8?style=for-the-badge&labelColor=07020f" alt="Play now in your browser (Unlimited XP)" height="40"></a> </p>

**PHASE is a vaporwave rhythm twin-stick shooter.** Your guns fire on the beat, every enemy you destroy plays a note in key, and your dashes are judged against the music to within 40 ms. A full run goes through twenty-five sectors and five guardians, backed by a soundtrack that is composed live as you play. There are no asset files in this repository because every pixel and every sound is generated at runtime.

![Title screen](screenshots/title.png)

| Sector 8 | Sector 25: Eclipse Omega Prime |
|---|---|
| ![A regular sector](screenshots/sector.png) | ![The final guardian](screenshots/guardian.png) |

https://github.com/user-attachments/assets/1874343a-22a3-412f-9892-e32110f42d5a

This repository holds two builds of the same game, and both are single, self-contained files:

|  | Web (original) | Windows (C port) |
| --- | --- | --- |
| File | [`web/phase.html`](web/phase.html) | [`c/phase95.c`](c/phase95.c) |
| Runs on | Any current browser with WebGL2 | Windows 95 through Windows 11 |
| Graphics | WebGL2 shaders, HDR render targets | Software rasterizer, GDI blit |
| Audio | WebAudio graph | Software synthesizer, `waveOut` |
| Input | Keyboard + mouse, gamepad, touch | Keyboard + mouse, joystick / gamepad |
| Saves | Browser `localStorage` | `PHASE95.SAV` next to the EXE |
| Build step | None | One compiler command |

---

## Contents

* [Quick start](#quick-start)
* [The game](#the-game)
* [Controls](#controls)
* [Technical notes: web build](#technical-notes-web-build)
* [Technical notes: C port](#technical-notes-c-port)
* [Building the C port](#building-the-c-port)
* [Benchmarking](#benchmarking)
* [Repository layout](#repository-layout)
* [License](#license)

---

## Quick start

**In a browser:** Download [`web/phase.html`](web/phase.html) and open it in any browser with WebGL2 support (Chrome, Edge, Firefox, or Safari 15 and later). The game loads two display fonts from Google Fonts when you are online, and falls back to system fonts when you are offline.

**On Windows:** Compile `PHASE95.EXE` using the command in [Building the C port](#building-the-c-port) (or download a prebuilt binary from Releases, if any exist) and run it. The game starts in fullscreen at the highest resolution your display supports. On first launch, it runs a short benchmark to measure your hardware speed and select a quality level. Your settings, high scores, and XP are saved to `PHASE95.SAV` in the same folder as the executable.

Wired headphones are recommended. Because a perfect dash has a timing window of ±40 ms, wireless audio latency is usually large enough to throw off your timing. If you use wireless headphones, adjust **AUDIO OFFSET** in Settings until on-beat dashes line up with what you hear.

---

## The game

### The beat

The entire simulation runs on one musical clock. Your guns fire on the rhythmic grid (starting at sixteenth notes), kills trigger notes in the current key, and enemies time their attacks to the music. Guardians change the time signature when they appear, and the Eclipse bends the tempo itself.

### Dashing on the beat

A ring on the arena floor closes in on your ship on every beat. If you dash as the ring lands on your ship, the game grades your timing against the beat:

| Timing | Window | Result |
| --- | --- | --- |
| **PERFECT** | ±40 ms | Refunds more energy than the dash cost and fires a large shockwave. A Perfect kill adds +1 combo. |
| **GREAT** | ±80 ms | Smaller refund and shockwave. |
| **GOOD** | ±125 ms | Smaller still. |

**Energy:** Dashing costs energy, and holding bullet time drains it continuously. The energy bar sits directly under your bombs, where one notch equals one dash. Energy refills slowly over time and jumps up with every kill, which means chaining Perfect dashes through enemy waves yields a net gain in energy.

### Surviving

* **Hitbox:** Only the bright dot at the centre of your ship can take damage, so you can graze bullets with the rest of the hull.


* **Combo:** Destroyed enemies drop sparks that raise your score multiplier, while taking a hit cuts your multiplier in half. Kills that land on the beat ring gold and play louder in the audio mix.


* **The arena:** The floor is a shallow basin with a turning tide. Because of this slope, coasting ships drift and long shots curve across the floor.


* **Bombs:** Detonating a bomb sends out a wave that erases enemy bullets and damages everything it reaches, including guardians.


* **Bullet time:** Holding the bullet time input slows down the action for as long as you have energy to spend.



### Sectors

A full run is 25 sectors long. Eleven enemy types (motes, darts, flickers, lungers, clusters, shards, gnats, beacons, bolas, vortices, and mortars) unlock as you get deeper into a run. Many sectors are built around a specific formation or hazard: *Gnat Cloud*, *Vortex Field*, *Pursuit*, *Lighthouses*, *Bola Storm*, *Lunge*, *Shatter*, *Noise Floor*, and *Bombardment*.

### Guardians

Every fifth sector ends with a multi-phase guardian fight scored to its own musical key and time signature:

| Sector | Guardian | Signature | Description |
| --- | --- | --- | --- |
| 5 | **Prism** | 4/4, D Dorian | A light-splitting crystal guarded by two counter-rotating shard triads |
| 10 | **Carousel** | 12/8, C# harmonic minor | A ring of twelve cars with a lead-aiming triple shooter and clockwise and counter-clockwise sprayers |
| 15 | **Eclipse** | 4/4, B♭ Phrygian | A dark disc moving along three elliptical orbits, with time dilation in phase III |
| 20 | **Eclipse Omega** | 4/4, then 12/8, then dilation | Spiral patterns, twice as many lasers, a aimed laser you must dash through on the beat, and boss dashes in phase III |
| 25 | **Eclipse Omega Prime** | Danmaku, 4/4, 12/8, dilation | The final sector: a larger, tougher Omega with dense bullet curtains and four times the lasers |

### Augments

Clearing a sector lets you pick one of three augment cards (graded common, rare, or epic, with an option to reroll the hand). These cards change how your ship fires and moves: faster fire rates on finer note subdivisions, spread shots, piercing and rebounding projectiles, homing rounds, rear fire, orbiting drones, chain lightning, exploding kills, a recharging shield, extra hull plating, a spark magnet, cheaper dashes, wider timing windows, extra bombs, missiles on every bar, periodic novas, higher movement speed, and heavy on-beat shots.

Augments marked with **♪** also add an instrument layer to the music. Depending on what you pick, the soundtrack gains an arpeggio that circles the stereo field with your drones, FM ticks on the offbeats, a brass stab on the first beat of the bar, a synth riser before every nova, or vocal chops on the accents.

### XP, bonuses and Level Select

At the end of a run, your score is converted into XP. You can spend XP in **Bonus** on six permanent upgrades (Extra Hull, Spare Shells, Hot Rounds, Tuned Thrusters, Combo Guard, and Second Opinion), or in **Level Select** to unlock later starting sectors (5, 10, 15, 20, and 25) and **Boss Rush**. The **Records** menu stores your best runs.

---

## Controls

| Action | Keyboard + mouse | Gamepad | Touch (web only) |
| --- | --- | --- | --- |
| Move | WASD / arrow keys | Left stick | Left thumb |
| Aim + fire | Mouse (always firing) | Right stick | Right thumb |
| Dash | Left click / Space | A / RB / RT | ⟫ button |
| Bullet time | Hold right click / Shift | Hold LT or X | Hold ⌛ button |
| Bomb | Q / E / middle click | B / LB | Bomb button |
| Pause | Esc / P | Start | Pause button |

On Windows, the game accepts any controller detected by WinMM and maps it to an Xbox-style layout. **Alt+Enter** or **F11** toggles fullscreen, and **Alt+F4** quits.

---

## Technical notes: web build

`web/phase.html` is the original game. It is about 4,800 lines of HTML, CSS, and JavaScript in a single file, written without external libraries or a build step.

**Rendering (WebGL2)**

* A perspective camera looks over a glossy floor that reflects the world above it.


* GPU particles are updated on the card using transform feedback.


* Gravity wells and explosions bend a spring grid on the floor, which is expanded into lines on the GPU.


* A synthwave sky shader draws the background gradient, banded sun, mountain ridge line, and stars.


* The scene renders in HDR into half-float (`RGBA16F`) targets before running a bloom pass.


* A final VHS and CRT composite shader handles tone mapping, scanlines, vignetting, chromatic aberration, shockwave refraction, and tape tracking wobble.



**Audio (WebAudio)**

* A tempo-mapped sequencer drives both the music and the game loop. Because BPM and time signature can change mid-song, gameplay systems read from the same tempo map so that gunshots, enemy attacks, and dash judging stay locked to the grid.


* Each sector composes its own music procedurally by choosing a key, mode, chord progression, arpeggio, and style, while player augments add extra instrument layers on top.


* The mix runs through stereo panning, convolution reverb, and a master compressor, with a user-adjustable audio offset to compensate for hardware latency.



**Interface**
Menus are plain HTML screens styled like retro OS windows floating over the WebGL canvas. Input handling supports keyboard and mouse, the browser Gamepad API, and symmetric on-screen twin sticks for touch devices. Settings, records, and XP are stored in `localStorage`.

---

## Technical notes: C port

`c/phase95.c` (about 6,700 lines of C99) is a complete rewrite of the web build in C. It was written to run on a Pentium-class PC under Windows 95 while preserving the visuals and audio of the browser version, and it also detects and uses modern CPU features when run on newer machines.

### Constraints

* **No C runtime:** The executable links no CRT. It provides its own entry point, memory and string helpers, and math routines (using x87 instructions in the 32-bit build).


* **Four system DLLs only:** The binary links only `KERNEL32`, `USER32`, `GDI32`, and `WINMM`, and its import table uses only functions that exist on Windows 95. Newer OS functions are queried dynamically at runtime.


* **Baseline CPU (i686):** The 32-bit build is compiled for Pentium Pro / II / III, Celeron, and Athlon processors using x87 floating point and no MMX or SSE instructions. Why? Because Windows 95 does not save SSE registers during context switches, so a plain i686 path is required for the binary to run safely on a real Windows 95 machine.


* **No assets, one file:** Graphics are drawn as vectors every frame, text uses a built-in vector font, and all music and sound effects are synthesized at runtime. One `.c` source file compiles directly into one `.exe`.



### Rendering pipeline

Each frame steps through seven stages (the stage names match the columns in the component benchmark):

1. **BG:** Draws the sky gradient, banded sun, mountain ridge, and stars using precomputed row tables (an exponential table and per-row floor values).


2. **PREP:** Updates the camera, blends palettes, and builds batches of 3D line segments for the world geometry, floor reflections, and dark vortex cores.


3. **RASTER:** Rasterizes the spring grid, glossy floor reflection, particles, and world geometry in software. Thin lines are drawn as Wu anti-aliased lines, while thick strokes and glows are drawn as anti-aliased capsules with soft halos. Light is accumulated additively into an HDR scene buffer that packs red, green, and blue into 10 bits each using saturating additions.


4. **BLOOM:** Downsamples the HDR buffer, runs a separable blur, and adds the glow back into the scene.


5. **COMP:** Applies tone mapping via lookup tables, bloom addition, scanlines, vignetting, shockwave refraction, chromatic aberration, desaturation, and VHS tracking wobble, writing the result to the output buffer (`RGB565` on 16-bit desktops, or `XRGB8888` on 32-bit displays).


6. **HUD:** Draws menus, text, and HUD elements directly onto the full-resolution output buffer using the vector font. Because this happens after the composite upscale, text stays sharp even when the scene renders at a lower quality setting.


7. **PRESENT:** Blits the finished frame from a DIB section to the window using GDI `BitBlt`.



**Quality levels** adjust the internal scene resolution before the composite pass scales it up to the window: **HIGH** renders at full output size, **MEDIUM** at half height, **LOW** at half width and height, and **LOWEST** at quarter size. **AUTO** adjusts scene resolution dynamically during gameplay to maintain frame rate. On first launch, the startup benchmark selects **LOW** if the frame rate is under 30 fps, **HIGH** if it is over 120 fps, and **MEDIUM** otherwise.

### Audio

A custom software synthesizer replaces the browser's WebAudio graph. Up to 64 voices (each with an oscillator, waveshaper, filter, and envelope) feed audio buses that match the web build: a music bus with ducking, tape wow, and a muffle filter; an effects bus; a Freeverb-style reverb; a gated "80s" reverb; a feedback delay; and a master compressor.

Voices are queued with absolute start times and mixed sample-accurately at 22.05 kHz stereo on a dedicated `waveOut` thread. Because audio mixing does not have to go through a browser audio graph, beat timing is tighter than in the web version, while the tempo map, procedural composition, and stereo panning remain identical.

### Simulation

In the C port, particle updates (which used GPU transform feedback in WebGL2) and the spring grid run on the CPU. All other game logic (enemies, guardians, augments, scoring, and dash timing) matches the web version.

### Display and settings

* **Resolution:** **BEST** (the default, which picks the largest mode listed by the display up to 3840×2400), **DESKTOP**, or fixed resolutions from 640×480 to 3840×2160. Fullscreen uses a borderless window when the chosen resolution matches the desktop, and switches the display mode otherwise.


* **Frame rate:** 30, 60 (default), 120, 240, 1000, or **UNLIMITED**.


* **Other settings:** Master, music, and effects volume, audio offset (default -60 ms), screen shake, bloom, CRT scanlines, VHS tracking, flash effects, FPS counter, and fullscreen.



### Performance on modern PCs

When the 32-bit executable starts up, it checks the CPU and OS to see what hardware features it can use beyond the Pentium baseline:

* **SIMD kernels:** `CPUID` and OS checks enable SSE2 or AVX2 versions of the hot pixel loops (composite, span fills, capsule rows, HUD strokes, scanline copies, and dims). These functions are compiled with target-specific attributes so that the rest of the executable stays plain i686 code. SIMD paths are enabled only on NT-family Windows (Windows 2000 and later), and AVX2 is used only when the OS supports saving AVX state. All kernel sets produce identical pixel output.


* **Gather tuning:** AVX2 gather instructions are fast on some CPUs and slow on others. A short timing test at startup measures both paths and only enables the gather-based composite if it is faster on the current CPU.


* **Threads:** Up to 16 worker threads process the background, world raster, bloom, composite, and HUD stages in horizontal row bands. The rasterizer divides the screen into finer bands that threads claim dynamically, so a dense cluster of bullets and particles in one part of the screen does not stall the other cores. Worker threads spin briefly between stages before sleeping, which keeps stage handoff times well under a microsecond instead of paying for an OS thread wake-up.


* **Present thread:** On multi-core NT systems, a separate thread blits finished frames from two alternating DIB sections so that GDI presentation overlaps with rendering the next frame.


* **Memory optimizations:** The renderer uses lookup tables, per-thread scratch rows, and non-temporal cache-bypassing stores at resolutions of 1920×1080 and above.



A separate **64-bit build** (`PHASE95_X64.EXE`) uses x86-64 SSE math throughout the entire program. It runs noticeably faster at low resolutions and cuts CPU load on the audio mixing thread by about two thirds.

At very high resolutions, frame rate is bottlenecked by memory bandwidth (a 4K frame requires writing 33 MB per frame) and the GDI blit. Adding a Direct3D 11 flip-model present path, loaded dynamically when available, would be the next step to remove the GDI bottleneck.

### Saves

`PHASE95.SAV` is a small versioned binary file stored next to the executable. It saves your settings, top ten records, XP, bonus levels, unlocked sectors, and first-start benchmark result, and automatically migrates save files from older versions.

---

## Building the C port

You need [MinGW-w64](https://www.mingw-w64.org/) GCC to compile the Windows builds. The commands below were tested with GCC 13 cross-compiling from Linux (`gcc-mingw-w64-i686` and `gcc-mingw-w64-x86-64`), and the same flags work with MinGW-w64 on Windows (such as MSYS2).

### Windows, 32-bit (runs on Windows 95 and later)

```sh
i686-w64-mingw32-gcc -std=gnu99 -O2 -march=i686 -mtune=pentium3 -mfpmath=387 -mno-sse -mno-mmx \
  -ffreestanding -fno-tree-loop-distribute-patterns -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-ident -nostdlib -mwindows -e _WinMainCRTStartup \
  c/phase95.c -o PHASE95.EXE -lkernel32 -luser32 -lgdi32 -lwinmm -lgcc
i686-w64-mingw32-strip PHASE95.EXE

```

Why these compiler flags are used:

* `-march=i686 -mfpmath=387 -mno-sse -mno-mmx`: Compiles plain i686 code with x87 floating-point math by default. This allows the binary to run on a Pentium Pro and on Windows 95, which does not save SSE registers. The SSE2 and AVX2 kernels are enabled per function in the source code and selected at runtime.


* `-nostdlib -ffreestanding -e _WinMainCRTStartup`: Excludes the C runtime library so the program can use its own entry point and helper functions.


* `-fno-tree-loop-distribute-patterns`: Prevents GCC from replacing memory-filling or copying loops with implicit calls to `memset` and `memcpy` inside our own implementations of those functions.


* `-lgcc`: Supplies compiler support routines for 64-bit integer division and stack probes.



**Test build:** Add `-DPH_TEST_XP=1000000000` and change the output to `-o PHASE95T.EXE`. This build tops up your save to 1,000,000,000 XP on every launch so you can test every bonus upgrade and starting sector immediately.

### Windows, 64-bit (Windows XP x64, Vista and later)

```sh
x86_64-w64-mingw32-gcc -std=gnu99 -O2 -march=x86-64 -mtune=generic \
  -ffreestanding -fno-tree-loop-distribute-patterns -fno-stack-protector \
  -fno-asynchronous-unwind-tables -fno-ident -nostdlib -mwindows -e WinMainCRTStartup \
  c/phase95.c -o PHASE95_X64.EXE -lkernel32 -luser32 -lgdi32 -lwinmm -lgcc
x86_64-w64-mingw32-strip PHASE95_X64.EXE

```

### Headless test build (Linux / macOS)

On Linux and macOS, the same `.c` file compiles into a headless test harness that runs the game loop on a simulated clock with the attract-mode AI piloting the ship, writing rendered frames as PPM files and audio as a WAV file:

```sh
cc -std=gnu99 -O2 c/phase95.c -o phase95 -lm -pthread
./phase95 --scene play --wave 5 --frames 1800 --shot 300 --out shots --wav run.wav

```

| Option | Meaning |
| --- | --- |
| `--scene title\|play` | Start on the title screen or in a run |
| `--wave N` | Starting sector (25 is the last) |
| `--frames N` | Number of frames to simulate |
| `--shot N --out DIR` | Write every Nth frame to `DIR` as a PPM file |
| `--wav FILE` | Write the mixed audio to a WAV file |
| `--res WxH`, `--fmt32` | Set output size; use 32-bit output instead of RGB565 |
| `--quality 0..4` | Set quality level (AUTO, HIGH, MEDIUM, LOW, LOWEST) |
| `--threads N` | Set worker thread count |
| `--simd 0\|1\|2` | Cap the SIMD kernel set (scalar, SSE2, AVX2) |
| `--god`, `--seed N` | Make pilot invulnerable; set random seed |
| `--compbench` | Run the component benchmark |

### Command-line options (Windows)

| Option | Meaning |
| --- | --- |
| `-w` | Start in windowed mode |
| `-bench` | Run the component benchmark and write `PHASE95_BENCH.TXT` |
| `-simd=0`, `-simd=1`, `-simd=2` | Cap the SIMD kernel set |
| `-d` | Enable debug keys |

---

## Benchmarking

Running `PHASE95.EXE -bench` simulates three scenes (title screen, Sector 3, and the Sector 25 guardian) across each supported SIMD kernel set using both one thread and all available threads. It then writes `PHASE95_BENCH.TXT` in the same folder with details on the build, CPU, supported instruction sets, OS version, thread count, gather tuning result, audio thread load, and milliseconds spent per frame in each stage (`SIM`, `BG`, `PREP`, `RASTER`, `BLOOM`, `COMP`, `HUD`, `PRESENT`, and total `FRAME`). During normal play, turning on **Settings → Show FPS** displays the same per-stage breakdown live on screen.

Example output (1920×1080, MEDIUM quality, running on a 2-core VM under Wine, so numbers are only indicative of relative stage cost):

```
SCENE     SIMD  THR  FPS      SIM     BG      PREP    RASTER  BLOOM   COMP    HUD     PRESENT FRAME
SECTOR 3  X87   1    67.3     0.145   1.246   0.130   2.248   0.974   5.634   1.686   2.617   13.18
SECTOR 3  AVX2  2    85.4     0.151   1.606   0.126   2.192   1.233   2.935   1.164   2.983   9.77
BOSS 25   X87   1    45.4     0.235   1.311   0.403   8.690   1.041   5.595   1.652   2.473   19.70
BOSS 25   AVX2  2    63.7     0.187   1.830   0.383   5.585   1.197   2.912   1.130   2.977   13.77

```

---

## Repository layout

```
.
├── README.md
├── LICENSE
├── web/
│   └── phase.html      the original HTML5 / WebGL2 game
├── c/
│   └── phase95.c       the single-file C port (Windows 95 to 11, plus the headless harness)
└── screenshots/        rendered by the C port

```

## Status

The C port has been tested using the headless harness (comparing rendered frames across scalar, SSE2, and AVX2 kernel sets as well as single and multi-threaded runs) and under Wine for both 32-bit and 64-bit builds. Test reports from real Windows 95 and Windows 98 hardware are very welcome.

## License

MIT. See [LICENSE](LICENSE).

https://github.com/user-attachments/assets/f396b360-000d-41ad-811d-270c14e77cdf



https://github.com/user-attachments/assets/1d7c3e32-0625-4faf-8804-68a9e69ce23b

