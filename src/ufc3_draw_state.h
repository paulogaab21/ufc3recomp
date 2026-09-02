#pragma once

#include <cstdint>

// Draw state, read straight out of the game's D3DDevice in guest memory.
//
// This is the foundation of the native renderer: the SDK's shader translator
// produces DXBC that expects the Xenos ABI -- float constants, boolean
// constants and the fetch constants -- and the game keeps exactly those in
// shadows inside the device itself. Reading them there is what connects one end
// to the other.
//
// See ufc3_draw_state.cpp for the offsets and where they came from.

namespace ufc3 {
namespace estado_desenho {

// A reading of the state at the moment of a draw. Addresses are guest-side.
struct Instantaneo {
  uint32_t device = 0;

  // Constant shadows, ready to become a constant buffer.
  uint32_t const_vs = 0;      // 256 vec4
  uint32_t const_ps = 0;      // 256 vec4
  uint32_t const_bool = 0;
  uint32_t fetch_const = 0;   // 32 slots x 6 dwords

  // Dirty masks: they say what changed since the last submission.
  uint64_t sujo_vs = 0;
  uint64_t sujo_ps = 0;
  uint64_t sujo_fetch = 0;

  // Index buffer currently bound.
  uint32_t obj_indices = 0;      // the game's object
  uint32_t indices_base = 0;     // address of the data in guest memory
  bool     indices_32bits = false;

  bool valido = false;
};

// Reads the state from the device pointer. `base` is the base of guest memory,
// as the recompiled code receives it.
Instantaneo Ler(uint8_t* base, uint32_t device);

// Checks the memory map against reality and logs the verdict, once. Call it
// from inside a draw hook, where a valid device pointer is in hand.
void ValidarUmaVez(uint8_t* base, uint32_t device);

}  // namespace estado_desenho
}  // namespace ufc3
