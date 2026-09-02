#pragma once

#include <cstdint>

// Scene extraction: reads from the game what it was about to draw, so the
// native renderer can draw it instead.
//
// This is skate3recomp's path: rather than translating the packets the game's
// D3D produces, we read the game's own structures and draw with new shading.
// That is what opens the door to MSAA, soft shadows and ambient occlusion --
// things emulation cannot give, because emulation is faithful to what the
// console did.
//
// See ufc3_extrai_cena.cpp.

namespace ufc3 {
namespace extrai_cena {

// One draw call, as the game described it.
struct Desenho {
  uint32_t tipo_primitiva = 0;
  uint32_t contagem_vertices = 0;
  int32_t  base_vertice = 0;
  uint32_t indice_inicial = 0;
  bool     indexada = false;

  uint32_t indices_base = 0;      // guest address
  bool     indices_32bits = false;

  // Raw fetch constants, the way the game keeps them. They are the key to the
  // geometry: the vertex buffer addresses come from them.
  uint32_t fetch[32 * 6] = {};
  uint32_t fetch_usadas = 0;
};

// Call from inside a draw hook. While capture is not armed, this costs one
// flag read.
void Observar(uint8_t* base, uint32_t device, uint32_t tipo, uint32_t contagem,
              int32_t base_vertice, uint32_t indice_inicial, bool indexada);

}  // namespace extrai_cena
}  // namespace ufc3
