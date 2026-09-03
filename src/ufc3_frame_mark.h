#pragma once

#include <cstdint>

// Frame boundary in the game's own terms.
//
// The native renderer needs to know where a frame begins and ends, so it can
// gather that frame's draws and issue them together. Until now the draw hooks
// counted calls with no idea where one frame stopped and the next started.
//
// There is no BeginScene equivalent in this game. The frame is not "opened": it
// starts when the command manager flips its write parity and the write buffer
// comes up empty. So the one real marker is the end, and the beginning is
// whatever follows it.
//
// See work/analise-quadro/quadro.md for the disassembly this rests on.

namespace ufc3 {
namespace frame_mark {

// Frames completed since startup, counted on the game's own boundary.
uint64_t Contagem();

// Called by the hook on 0x82747750, the game's end-of-frame.
void AoFecharQuadro(uint8_t* base);

}  // namespace frame_mark
}  // namespace ufc3
