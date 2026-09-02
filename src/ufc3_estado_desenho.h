#pragma once

#include <cstdint>

// Estado de desenho do jogo, lido direto do D3DDevice na memoria do guest.
//
// Este e o alicerce do renderizador nativo: o tradutor de shader do SDK produz
// um DXBC que espera a ABI do Xenos -- constantes float, booleanas e as fetch
// constants -- e o jogo mantem exatamente essas coisas em sombras dentro do
// proprio device. Ler dali e' o que liga uma ponta na outra.
//
// Ver ufc3_estado_desenho.cpp para os deslocamentos e de onde vieram.

namespace ufc3 {
namespace estado_desenho {

// Uma leitura do estado no instante de um desenho. Enderecos sao do guest.
struct Instantaneo {
  uint32_t device = 0;

  // Sombras das constantes, prontas para virar constant buffer.
  uint32_t const_vs = 0;      // 256 vec4
  uint32_t const_ps = 0;      // 256 vec4
  uint32_t const_bool = 0;
  uint32_t fetch_const = 0;   // 32 slots x 6 dwords

  // Mascaras de "sujo": dizem o que mudou desde o ultimo envio.
  uint64_t sujo_vs = 0;
  uint64_t sujo_ps = 0;
  uint64_t sujo_fetch = 0;

  // Buffer de indices em vigor.
  uint32_t obj_indices = 0;      // objeto do jogo
  uint32_t indices_base = 0;     // endereco dos dados no guest
  bool     indices_32bits = false;

  bool valido = false;
};

// Le o estado a partir do ponteiro do device. `base` e a base da memoria do
// guest, como o codigo recompilado a recebe.
Instantaneo Ler(uint8_t* base, uint32_t device);

// Confere se o mapa de memoria bate com a realidade e registra o veredito no
// log, uma unica vez. Chamar de dentro de um gancho de desenho, quando ha
// device valido em maos.
void ValidarUmaVez(uint8_t* base, uint32_t device);

}  // namespace estado_desenho
}  // namespace ufc3
