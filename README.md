# UFC 3 Recomp

An unofficial native recompilation of the Xbox 360 version of **UFC Undisputed 3**
for PC, built on the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk).

This is not emulation. The game executable is translated from PowerPC to C++ and
then compiled as a native PC program — the result is an `.exe` that runs directly
on your processor, with nothing interpreted at runtime.

The project does not include UFC Undisputed 3 retail game files. To run or build
it, you must provide files from your own legally obtained Xbox 360 copy.

Gameplay (click to watch on YouTube):

<p align="center">
  <a href="https://www.youtube.com/watch?v=5cQYiwqAFsc">
    <img src="https://i.ytimg.com/vi/hUpWKWqRkJ8/maxresdefault.jpg" alt="UFC Undisputed 3 recompiled — playable at 2K" width="480">
  </a>
</p>

---

## How Do I Play?

You do not need to compile anything. The [release](../../releases/latest) ships a
ready executable.

1. Download and extract the release
2. Run **UFC3 Launcher.exe**
3. Point it at the ISO of your own disc, or at an already-extracted folder
4. Click **Extract and play**

The launcher reads the XEX header and checks the title ID (`5451087D`) before
going any further; if you point it at a different game, it tells you which one it
found.

Only Windows x64 is tested. `CMakePresets.json` also lists Linux, macOS and
ARM64 — none of those have been exercised here, so treat them as a starting
point, not as support.

---

## Frame rate, and why 60 Hz is the right number

Worth stating up front, because it is counterintuitive and it will save you from
a bad setting.

The runtime derives the guest vblank from `video_mode_refresh_rate`, and this
game advances **one simulation step per vblank**. At 144 Hz the simulation runs
2.4x faster than on console: the image is smooth, but the game runs fast. Raising
it further would not be an improvement — it would be the game in fast-forward.

The same mechanism has a second consequence. A saturated GPU does **not** lower
your frame rate here; it lowers the **speed of the game**. That is why render
supersampling is locked at 1x: measured on an RTX 3060 Ti during a fight, 1x
keeps the GPU around 25% while 2x pushes it to 98%, and the game slows down. An
option whose effect a player cannot interpret is not an option, it is a trap.

So the number that matters is not frames per second, it is the vblank: **60 Hz is
the correct speed**, and it is the default. The PC gain comes from resolution and
filtering, not from refresh rate.

---

## Native Renderer

**Status: foundation in place, scene rendering not implemented.**

The long-term goal is the one skate3recomp reached: draw the game directly
through Direct3D 12 by reading the game's own structures, instead of emulating
the Xbox 360 GPU. That is where real visual gains live — MSAA, soft shadows,
ambient occlusion — because they require replacing the console's shading rather
than reproducing it.

What actually ships today:

- A native render hardware interface (Direct3D 12 and Vulkan) is integrated and
  runs every frame, with its own pipelines, shaders, intermediate targets and
  barriers.
- It owns the **final image**: contrast-adaptive sharpening recovers definition
  lost when the game's 1280x720 output is scaled to your display, and the
  settings menu gets a blurred, darkened backdrop.
- Emulated-pass suppression is wired, so the native path can take over passes one
  at a time once it can draw them.

What does **not** ship yet: the scene itself. Every frame is still drawn by the
emulated GPU path. The native renderer post-processes the finished image; it does
not yet draw geometry.

Groundwork that is done and verified against the running game:

- The game's `D3DDevice` memory map, including the shader-constant shadows and
  their dirty masks
- The vertex shader microcode location
- Confirmation that the SDK's Xenos shader translator can be reused, which
  removes the hand-written shader port that dominated skate3recomp's effort

Still missing: the pixel shader microcode field, the vertex declaration, the
texture object layout, native render targets to replace EDRAM emulation, and the
scene and material assembly on top.

---

## The launcher

A single executable, with its interface, images and fonts embedded — there is no
loose file for anyone to tamper with.

- Accepts your disc image or an already-extracted folder
- Adjusts resolution, quality presets, language and controls, writing everything
  to the same `ufc3.toml` the in-game settings screen uses
- Works two ways, decided by a single test: is there a `ufc3.exe` next to it? If
  so, extract and play. If not, translate and compile.

Source in [`launcher/`](launcher).

---

## How it works

```
default.xex  (PowerPC, big-endian, Xbox 360)
     |
     |  rexglue codegen  — disassembles and translates instruction by instruction
     v
generated/default/*.cpp  (C++, ~295 MB)
     |
     |  clang + the ReXGlue runtime
     v
ufc3.exe  (native x86-64)
```

ReXGlue provides both halves: the **compiler**, which does the translation, and
the **runtime**, which is an Xbox 360 reimplemented in C++ — kernel, Xenos GPU
translated to D3D12, audio, input, filesystem.

This repository is the part in the middle: telling the recompiler where each
function begins and ends, correcting what it cannot discover on its own, and
implementing what the game expects from the console that the runtime does not yet
provide.

Current state of the build:

- The whole `default.xex` is translated to C++ — 569 files, 295 MB of code
- That compiles and links into a 93 MB native executable
- **66,157 recompiled functions** registered in the function table
- Xbox 360 kernel imports resolved: 101 from `xam`, 184 from `xboxkrnl`
- Codegen produces no warnings — the manifest covers every function the scanner
  cannot reach by itself

---

## The core problem: finding function boundaries

The recompiler discovers functions two ways: by following calls (`bl`) from the
entry point, and by recognising the prologue — the instructions every normal
function runs on entry, saving the return address and reserving stack space.

One class of function escapes both:

- **no prologue** — it is small enough that the 2011 compiler did not emit one
- **not the target of any `bl`** — it is only reached indirectly, through a static
  constructor table, a vtable, or a function pointer

And for the same reason (it never touches the stack), it does **not appear in the
`.pdata` section** either, which is the exception-unwind directory. Confirmed in
practice: of the 47,145 functions `.pdata` lists, none covers these cases.

Four of them blocked code generation and were fixed by hand:

| Address | What it is | Size |
|---|---|---|
| `0x82691B80` | leaf function cut off at a tail call | `0x78` |
| `0x82F451D0` | vtable thunk, reached only by `bctr` | `0x48` |
| `0x8232F760` | multiple-inheritance adjustor thunk | `0x8` |
| `0x82314918` | leaf getter | `0x2C` |

A fifth (`0x831820A8`, a static initializer) only showed up at runtime.

Solving these case by case does not scale. There is a detector in
[`tools/find_orphans.py`](tools/find_orphans.py) that disassembles the 17 MB of
code, cross-references the functions the recompiler already knows, and looks for
valid code that is not the target of any direct branch. It finds the confirmed
case with the exact size — but its **end**-of-function detection is still naive
(it stops at the first terminator, and a real function has several internal
blocks), which produces false positives on `switch` blocks. Improving that to
follow control flow properly is probably the most valuable contribution anyone
can make here.

---

## Building from source

This is for people who want to run the translation on their own machine. To just
play, use the [release](../../releases/latest) — it needs no tools at all.

First, extract from your disc:

```
assets/game/default.xex
```

Without that file there is nothing to recompile.

Then: Clang 18+, CMake 3.25+, Ninja and the ReXGlue SDK. On Windows all of that
except the SDK comes with the **MSVC Build Tools** — Clang uses their headers and
libraries, and linking happens against the Microsoft ABI. You do not need to
install cmake or ninja separately.

```bash
# 1. translate the XEX to C++
rexglue codegen ufc3_manifest.toml

# 2. configure
cmake --preset win-amd64-relwithdebinfo -DREXSDK_DIR=<path to rexglue-sdk>

# 3. build
cmake --build out/build/win-amd64-relwithdebinfo

# 4. run (pointing at your extracted disc folder)
./out/build/win-amd64-relwithdebinfo/ufc3.exe --game_data_root "<game folder>"
```

To investigate a crash, `--log_file <path> --log_verbose` walks the whole boot
step by step.

---

## Findings sent back to the SDK

Building this from scratch on Windows surfaced defects in the ReXGlue SDK. The
first three are the same family — **consuming the SDK from source, exactly the
way `rexglue init` documents, was broken**:

1. **`MSPACK_DIR` pointing at a symlink tree.** On Windows git materializes a
   symlink as a 29-byte text file by default, and clang tries to compile it as C.
   Breaks on any Windows clone.

2. **Generated presets without `-march`.** The SDK's byte-swap routines use SSSE3
   intrinsics — needed because the Xbox 360 is big-endian and the PC is not. The
   SDK declares that ISA only in its own presets; the presets `rexglue init`
   generates had nothing, so the internal libraries compiled without SSSE3 and
   failed.

3. **`imgui` linked PRIVATE but exposed in a public header.** `rex/ui/style.h`
   includes `<imgui.h>` and `rex/rex_app.h` pulls it in transitively — so every
   generated project needs that include, but it did not propagate.

The rest appeared later, with the game already running:

4. **Misleading codegen warning.** `emitBranchWithBoundsCheck` warned about any
   branch leaving the function, but dispatch thunks do that constantly by
   construction. The warning hid the cases that actually matter. The fix is
   written but **not yet validated**.

5. **`user_data_root` from the TOML has no effect.** In `ui/rex_app.cpp` paths are
   resolved from the cvars **before** `LoadConfig()` reads the file, so a value
   placed in the TOML arrives too late and the default folder wins — silently,
   creating a second set of saves.

6. **The SDK version is read from the wrong repository.** `rex_resolve_version`
   defaults to `CMAKE_SOURCE_DIR`, which points at the **consuming** project's
   root when the SDK enters through `add_subdirectory`. Creating a `v1.0.0` tag
   here was enough to make configure abort with *"floor version (0.10) is behind
   tag version (1.0)"*.

7. **String cvars written to TOML without escaping.** `SaveConfig` serialized a
   string as `name = "value"` with no escaping. A Windows path made the whole file
   unparseable — `\U` is a Unicode escape — so every setting silently reverted on
   the next start, including `gpu_plugin`, and the game came up with no GPU
   emulation at all: a black screen, with nothing in the log pointing at the
   cause. Opening the settings screen once was enough to trigger it.

---

## Roadmap

- [x] Translate the XEX to C++
- [x] Build a native executable
- [x] Bring the runtime up and load the game
- [x] Get past the early static initializers
- [x] Reach the first rendered frame
- [x] Reach playable gameplay
- [x] Warning-free manifest (67 verified entries)
- [x] Career saves working, in one place
- [x] Launcher: from ISO to running game, with no tools required
- [x] Release ready to play
- [x] Native render interface integrated, owning the final image
- [ ] Locate the pixel shader microcode
- [ ] Decode the vertex declaration and texture object layout
- [ ] Native render targets in place of EDRAM emulation
- [ ] Native scene rendering
- [ ] Torso textures in Create-a-Fighter
- [ ] Improve the orphan-function detector (follow control flow)
- [ ] Resolve the jump tables (`[[switch_tables]]`) — 1,067 already extracted
- [ ] Open the remaining SDK pull requests

---

## About

I am Brazilian, 23, a software engineering student. This is a free-time project —
it started from curiosity about how a static recompilation actually works, and
turned into this.

No deadlines and no promises. I publish what comes out.

If you know PowerPC, Xbox 360 reverse engineering, or just want to follow along,
issues and discussions are welcome. [CONTRIBUTING](CONTRIBUTING.md) says where
help makes the most difference — and, more importantly, **what I already tried
that did not work**, so nobody repeats it. The analysis tools are in
[`tools/`](tools/), each one explained.

---

## Legal

A research and reverse-engineering project for interoperability.

What the release distributes is the console executable **translated** to C++ and
compiled as a native program — a derived work, produced by this project. No game
data ships with it: art, audio, video, models and fighter attributes still come
from the disc you bought, and without it the program does not even start.

UFC Undisputed 3 is a trademark and property of THQ / Yuke's / their respective
owners, with no relationship to this project.
