#pragma once

#include <cstdint>

// Locating the game's shader microcode in guest memory.
//
// This is the last link in the chain that lets the native renderer draw. The
// SDK already carries a complete Xenos shader translator: hand it microcode and
// it gives back DXBC ready for CreateGraphicsPipelineState. What was missing was
// knowing where the game keeps that microcode.
//
// Two static-analysis passes reached the same offsets independently, one from
// the disassembly and one from a PM4 opcode sweep, which is the strongest
// guarantee available without running the game. See work/analise-render/
// achados.md, sections 10 and 11.
//
// One consequence worth stating, because it removes work rather than adding it:
// there is no vertex declaration object in this game. The vertex layout lives in
// the `vfetch` instructions of the microcode itself, so the translator reports
// it after analysis. Only the stride lives outside, in the device.

namespace ufc3 {
namespace shader_fetch {

// A shader as the game holds it: where the microcode is, and how much of it.
struct Microcodigo {
  uint32_t objeto = 0;     // the game's shader object
  uint32_t endereco = 0;   // microcode address in guest memory
  uint32_t dwords = 0;     // length, in dwords, which is what the translator wants
  bool     pixel = false;
  bool     valido = false;
};

// Reads the currently bound pixel or vertex shader out of the device.
Microcodigo LerPixelShader(uint8_t* base, uint32_t device);
Microcodigo LerVertexShader(uint8_t* base, uint32_t device);

// Logs what it found for the current draw, once. The check that matters is
// whether the first words look like Xenos control flow rather than zeros.
void ValidarUmaVez(uint8_t* base, uint32_t device);

}  // namespace shader_fetch
}  // namespace ufc3
