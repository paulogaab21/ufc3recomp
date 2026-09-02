# Analysis tools

The hard work in this recompilation is not graphics or audio: it is finding where
each function begins and ends inside the console executable. These are the tools
I used for that.

None of them is magic. They all start from the disassembled executable and from
what the recompiler already knows, and they return **candidates** — the final
call was always mine, one at a time. Trying to automate that decision is exactly
what went wrong: I once generated 4,751 automatic manifest entries and harvested
2,919 latent failures, while 8 hand-verified entries produced zero warnings.

## Configuration

Nothing here has a hardcoded path. Three environment variables control
everything:

| Variable | What it is | Default |
|---|---|---|
| `UFC3_ROOT` | project root | the folder containing `tools/` |
| `UFC3_GAME` | extracted disc folder | — required to run the game |
| `REXSDK_SRC` | ReXGlue SDK source | — needed for the PowerPC objdump |

```powershell
$env:UFC3_GAME  = "D:\UFC3_extracted"
$env:REXSDK_SRC = "C:\dev\rexglue-sdk"
```

Some scripts expect to find the disassembly (`code.dis`) and the basefile
(`base.bin`) in `work/`. Those come from your own disc and are therefore not in
this repository.

## The three kinds of failure, and why they matter

Before the tools, the map. The runtime fails three ways, and **two of them call
for opposite fixes**:

| Message | What it means | What to do |
|---|---|---|
| `Call to invalid or unregistered function at 0xA` | the function at `0xA` does not exist | **add** it to the manifest |
| `Unresolved branch from 0xA to 0xB` | the function containing `0xA` was cut short | **remove** entries in that region |
| `Unresolved call from 0xA to 0xB` | same | same |

Telling them apart is a matter of checking whether a manifest entry exists in
that region. Confusing them means hours spent going the wrong way — which is what
happened to me.

## Finding missing functions

**`add_missing.py`** — given a crash address, computes the end of the function by
walking the flow graph and adds it to the manifest. It recognises adjustor thunks
(`addi rX,rX,N` followed by `b target`, 8 bytes) and rejects sparse bodies, where
the walked path covers less than 70% of the range — a sign the end was estimated
wrong.

**`varre_regiao.py`** — sweeps a range applying the four tests a candidate
function must pass: not already known, not the target of any direct branch,
preceded by a terminator, and with a contiguous body that closes. With
`--aplicar`, it writes to the manifest.

**`find_orphans.py`** and **`find_orphans2.py`** — bulk detectors. They
disassemble the 17 MB of code, cross-reference what the recompiler knows, and
look for valid code nobody calls directly. The first stops at the first
terminator, which produces false positives on `switch` blocks; the second follows
control flow. **Treat the output as a candidate list, never as truth.**

**`acha_thunks.py`** — looks for multiple-inheritance adjustor thunks. It requires
`addi r3,r3,N` (the `this` pointer only) and that the address is not a branch
target. That second filter is essential: without it I picked up 16 internal
blocks of existing functions, and linking broke with `use of undeclared label`.

**`acha_despachantes.py`** — looks for frameless vtable dispatchers: the pattern
`lwz r,0(r3)` / `lwz r,N(r)` / `mtctr` / `bctr` in 16 bytes, with the same
branch-target filter. Of 41 candidates, only 1 survived it.

**`acha_switch.py`** — extracts jump tables. It finds the index register from
`rlwinm rD,rIndex,2,0,29` and reads the labels big-endian from `base.bin`,
validating that they all land inside the code range. It extracted 1,067 tables,
**not yet applied to the manifest**.

**`filtra_manifesto.py`** — prunes the manifest, removing entries that do not hold
up.

## Automatic loops

**`crash-loop.ps1`** — runs the game, classifies the failure into the kinds above,
and disassembles the region with source and destination marked. With `-Corrigir`
it removes entries on its own in the cut-function case.

**`loop-faltantes.ps1`** — a closed loop for the *missing function* case: run,
take the address, compute the end, add it, regenerate, rebuild, repeat. It only
acts on that case; any other error stops it and hands the result back for human
analysis. Adding a missing function is the safe direction — with the right size,
it truncates nothing. Removing or resizing touches code that already works, and
that does not go into automation.

**`auto-loop.ps1`** — the mirror image: it only acts on the *cut function* case,
which has a mechanical fix.

## Measurement

**`medir.ps1`** — measures CPU, GPU, threads and resolve rate at the same time,
which is what separates the hypotheses:

```
GPU high + CPU low        -> GPU-bound
GPU low + 1 thread at 100% -> bound in the recompiled code
Textures/s high           -> cache too small, constant reloading
Resolves/s < 60           -> the simulation is not keeping up with vblanks
```

That last line is the most important and the least obvious: in this engine the
game advances one simulation step per vblank, so saturating the GPU **does not
lower the frame rate — it lowers the speed of the game**.
