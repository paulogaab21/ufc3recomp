# Contributing

If you came here wanting to help: thank you. This document says where help makes
the most difference, and what I already tried that did not work — so you do not
spend time repeating it.

## Before anything else

You need your own copy of UFC Undisputed 3. Nothing from the game is in this
repository, and **a pull request carrying any file from the disc will be
rejected** — `.xex`, `.iso`, the generated C++, textures, audio, saves. The
`.gitignore` already blocks the obvious cases, but check before opening one.

To set up the environment, see *Building from source* in the [README](README.md).
The analysis tools are in [`tools/`](tools/README.md), with a document explaining
each one.

## The core problem

It is not graphics, audio, or performance. It is **finding where each function
begins and ends** inside the console executable.

The recompiler finds functions by following calls (`bl`) and recognising
prologues. One class escapes both: functions too small to have a prologue,
reached only indirectly — through a vtable, an adjustor thunk, or a static
constructor table. And because they never touch the stack, they do not appear in
`.pdata` either. Of the 47,145 functions `.pdata` lists, none covers these cases.

Every one of them was found and verified by hand. They are the 67 entries in
`ufc3_manifest.toml`.

## Where to help, in order of value

### 1. End-of-function detection by control flow

`tools/find_orphans.py` stops at the first terminator, and a real function has
several internal blocks — which produces false positives on `switch` blocks.
`find_orphans2.py` already follows the flow, but still gets it wrong. Getting
this right is probably the most valuable contribution possible here: it unblocks
everything else.

A candidate is only acceptable if it passes all four tests: it is not an already
known function, it is not the target of any direct branch, it comes after a
terminator, and it has a contiguous body that closes.

### 2. Apply the jump tables

`tools/acha_switch.py` already extracted **1,067 tables**, with address, index
register and labels validated against the code range. They have never been
applied to the manifest. Applying them and measuring the effect is well-bounded
work.

### 3. Torso textures in Create-a-Fighter

An open, reproducible bug. What has already been ruled out: it is not an exotic
format — the log shows no `k_DXN`, `k_DXT3A` or `k_CTX1`, only well-supported
ones. The current suspicion is the render-to-texture composition path the game
uses for tattoos and logos.

### 4. Native renderer

The long-term goal is drawing the game directly through D3D12 by reading the
game's own structures, instead of emulating the console GPU. The foundation is
in place and the final image already goes through it, but the scene does not yet.

What is missing, in order: the pixel shader microcode field, the vertex
declaration, the texture object layout, native render targets in place of EDRAM
emulation, and then scene and material assembly.

Two things are already settled and save a lot of work: the SDK's Xenos shader
translator **can be reused**, so the game's shading does not need to be ported by
hand, and the vertex shader microcode has been located inside the game's
`D3DDevice`.

### 5. Validate SDK fix #4

It is written and compiles, but has **never been validated**. It needs a separate
build directory — it is not worth risking the playable build.

## What I already tried that did not work

Worth reading before proposing.

**Brute-forcing the manifest.** I generated 4,751 automatic entries: 2,919 latent
failures. I got it down to 1,874. Meanwhile, 8 hand-verified entries produced
zero warnings. **Quantity is not progress here** — every wrong entry truncates a
function that used to work.

**Batch vtable dispatchers.** 41 candidates, link broken with `use of undeclared
label`: they were branch targets, meaning internal blocks of existing functions.
With a branch-target filter, only 1 survived. The same mistake cost me 16 false
thunks before I applied the filter to both detectors.

**Non-square render scaling.** 2x1, to gain sharpness at half the cost: it
corrupts character textures. Also tested with
`draw_resolution_scaled_texture_offsets = false`; still corrupts. Both axes have
to move together.

**Raising the refresh rate.** It does not make the game smoother: the guest vblank
comes from `video_mode_refresh_rate` and the game advances one simulation step
per vblank. At 144 Hz it runs 2.4x faster. For the same reason, saturating the
GPU lowers the game's **speed**, not the frame rate.

**Unlocking render supersampling.** Same trap, found the expensive way. Letting
players pick 2x dropped a fight from 60 to 35 fps — and worse, a `2x` saved by an
older version came back to life on its own when the lock was removed, so the game
got slower after an update without anyone choosing anything. It is locked at 1x.

## How to propose a manifest change

For each entry, say:

1. the address and the size;
2. how you arrived at that end — the path through the flow graph;
3. why it is not the target of a direct branch;
4. what changes in the game, measured: codegen warnings before and after, and how
   far it runs.

An entry without justification does not go in, even if it looks right. I have
already lost too much time to well-meaning guesses — including my own.
