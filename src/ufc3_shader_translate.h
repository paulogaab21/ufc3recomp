#pragma once

#include <cstdint>

// Translating the game's shaders into DXBC the PC can run.
//
// This closes the chain the native renderer needs:
//
//   draw hook -> device -> shader object -> descriptor -> microcode
//                                                            |
//                                          SDK Xenos translator
//                                                            |
//                                                          DXBC
//
// The translator is the SDK's own, the one the emulated path already uses in
// production. skate3recomp ported Skate 3's shading to HLSL by hand, which was
// the bulk of its ~30,000 lines; reusing the translator removes that work
// entirely.
//
// See ufc3_shader_translate.cpp for what the produced DXBC does and does not
// expect.

namespace ufc3 {
namespace shader_translate {

// Translates the shader currently bound in the device, reading its microcode
// from guest memory. Logs the outcome once per shader hash, so a scene with a
// handful of materials reports a handful of lines instead of thousands.
void TraduzirUmaVez(uint8_t* base, uint32_t device);

// How many distinct shaders have been translated, and how many failed.
void Resumo(uint64_t& traduzidos, uint64_t& falharam);

}  // namespace shader_translate
}  // namespace ufc3
