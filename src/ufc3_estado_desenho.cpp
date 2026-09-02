// ufc3 - estado de desenho lido do D3DDevice do jogo
//
// POR QUE ISTO EXISTE
//
// O renderizador nativo precisa, a cada desenho, do mesmo estado que a GPU
// receberia: constantes de shader, fetch constants, buffers de vertice e
// indice, texturas. Hoje esse estado vira pacotes PM4 e o emulador o reconstroi
// lendo o anel de comandos de volta -- informacao que o jogo TINHA e jogou fora.
//
// Duas investigacoes independentes se encontraram aqui:
//
//   Do lado do SDK, o tradutor de shader do Xenos e' reutilizavel: entregando o
//   microcodigo, ele devolve DXBC pronto para CreateGraphicsPipelineState. Mas
//   o DXBC nao e' autossuficiente -- ele espera a ABI do Xenos: constant buffers
//   de constantes float, booleanas, de laco e de fetch, mais a memoria fisica do
//   guest como ByteAddressBuffer, porque o vertex shader traduzido faz o proprio
//   vfetch em vez de usar o Input Assembler.
//
//   Do lado do jogo, a analise estatica mapeou onde o D3D9 do XDK guarda
//   exatamente essas coisas: sombras dentro do proprio D3DDevice, com mascaras
//   de "sujo" que dizem o que mudou desde o ultimo envio.
//
// Ou seja: e' a MESMA ABI dos dois lados. O que este arquivo faz e' ler a sombra
// do jogo para poder alimentar o tradutor do SDK -- e, antes disso, CONFERIR que
// o mapa esta certo. Construir milhares de linhas sobre deslocamentos nao
// verificados seria caro de descobrir depois.
//
// O MAPA (work/analise-render/achados.md, secao 8.0)
//
//   device+0x0000  mascara suja das constantes de VS   (64 bits, 1 bit / 4 vec4)
//   device+0x0008  mascara suja das constantes de PS
//   device+0x0018  mascara suja das fetch constants    (bit 32+slot)
//   device+0x0480  fetch constants: 32 slots x 6 dwords = 768 B   (reg 0x4800)
//   device+0x0780  constantes float do VERTEX shader: 256 vec4    (reg 0x4000)
//   device+0x1780  constantes float do PIXEL shader:  256 vec4    (reg 0x4400)
//   device+0x2780  constantes booleanas                           (reg 0x4900)
//   device+0x318C  objeto do buffer de indices em vigor
//
//   objeto de indices:  +0x00 formato/flags (bit 0x80000000 = indices de 32
//                       bits), +0x18 endereco base dos dados no guest
//
// A validacao abaixo nao confia no mapa: ela procura contradicoes. Um endereco
// de indices fora do intervalo do guest, ou constantes inteiramente zeradas
// durante uma cena com geometria, sao sinais de que algum deslocamento esta
// errado -- e e' melhor saber disso pelo log do que por uma tela preta.

#include "ufc3_estado_desenho.h"

#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace ufc3 {
namespace estado_desenho {

namespace {

// Deslocamentos dentro do D3DDevice.
constexpr uint32_t kSujoVs      = 0x0000;
constexpr uint32_t kSujoPs      = 0x0008;
constexpr uint32_t kSujoFetch   = 0x0018;
constexpr uint32_t kFetchConst  = 0x0480;
constexpr uint32_t kConstVs     = 0x0780;
constexpr uint32_t kConstPs     = 0x1780;
constexpr uint32_t kConstBool   = 0x2780;
constexpr uint32_t kObjIndices  = 0x318C;

// Dentro do objeto de buffer de indices.
constexpr uint32_t kIdxFormato  = 0x00;
constexpr uint32_t kIdxBase     = 0x18;
constexpr uint32_t kIdx32Bits   = 0x80000000u;

// O guest do Xbox 360 enderecana em 32 bits; a imagem do UFC 3 comeca em
// 0x82000000. Enderecos fora de uma faixa plausivel denunciam deslocamento
// errado antes de virarem acesso invalido.
constexpr uint32_t kGuestMin = 0x00010000u;
constexpr uint32_t kGuestMax = 0xE0000000u;

// Mesma aritmetica dos macros do codigo gerado (ufc3_pch.h), reproduzida aqui
// porque este arquivo nao inclui o cabecalho pre-compilado do codegen.
inline uint32_t DeslocamentoFisico(uint32_t endereco) {
#if defined(_WIN32)
  return endereco >= 0xE0000000u ? 0x1000u : 0u;
#else
  (void)endereco;
  return 0u;
#endif
}

inline uint32_t LerU32(uint8_t* base, uint32_t endereco) {
  uint32_t v = 0;
  std::memcpy(&v, base + endereco + DeslocamentoFisico(endereco), sizeof v);
  return __builtin_bswap32(v);
}

inline uint64_t LerU64(uint8_t* base, uint32_t endereco) {
  uint64_t v = 0;
  std::memcpy(&v, base + endereco + DeslocamentoFisico(endereco), sizeof v);
  return __builtin_bswap64(v);
}

inline bool EnderecoPlausivel(uint32_t a) { return a >= kGuestMin && a < kGuestMax; }

std::atomic<bool> g_ja_validou{false};

}  // namespace

Instantaneo Ler(uint8_t* base, uint32_t device) {
  Instantaneo e;
  if (!base || !EnderecoPlausivel(device)) {
    return e;
  }
  e.device = device;

  // As sombras nao sao lidas: seus ENDERECOS e que interessam, porque e' de la
  // que o constant buffer vai ser preenchido sem copia intermediaria.
  e.const_vs    = device + kConstVs;
  e.const_ps    = device + kConstPs;
  e.const_bool  = device + kConstBool;
  e.fetch_const = device + kFetchConst;

  e.sujo_vs    = LerU64(base, device + kSujoVs);
  e.sujo_ps    = LerU64(base, device + kSujoPs);
  e.sujo_fetch = LerU64(base, device + kSujoFetch);

  e.obj_indices = LerU32(base, device + kObjIndices);
  if (EnderecoPlausivel(e.obj_indices)) {
    const uint32_t formato = LerU32(base, e.obj_indices + kIdxFormato);
    e.indices_base   = LerU32(base, e.obj_indices + kIdxBase);
    e.indices_32bits = (formato & kIdx32Bits) != 0;
  }

  e.valido = true;
  return e;
}

void ValidarUmaVez(uint8_t* base, uint32_t device) {
  bool esperado = false;
  if (!g_ja_validou.compare_exchange_strong(esperado, true)) {
    return;
  }

  const Instantaneo e = Ler(base, device);
  if (!e.valido) {
    REXLOG_ERROR("ufc3 estado: device 0x{:08X} nao e' um endereco plausivel do guest", device);
    return;
  }

  // As fetch constants sao o teste mais severo do mapa: cada slot tem 6 dwords,
  // e o primeiro carrega tipo e endereco base. Um slot em uso jamais e' todo
  // zero, e o endereco tem de cair dentro do guest. Se nada disso valer para
  // nenhum dos 32 slots, o deslocamento 0x0480 esta errado.
  uint32_t slots_usados = 0;
  uint32_t slots_plausiveis = 0;
  for (uint32_t s = 0; s < 32; ++s) {
    const uint32_t d0 = LerU32(base, e.fetch_const + s * 24);
    const uint32_t d1 = LerU32(base, e.fetch_const + s * 24 + 4);
    if (!d0 && !d1) {
      continue;
    }
    ++slots_usados;
    // Nos dois primeiros dwords o endereco vem deslocado 2 bits (unidade de
    // dwords), no formato do xe_gpu_vertex_fetch_t / texture_fetch_t.
    const uint32_t endereco = (d0 & 0x3FFFFFFCu) << 2;
    if (EnderecoPlausivel(endereco)) {
      ++slots_plausiveis;
    }
  }

  REXLOG_INFO(
      "ufc3 estado: device 0x{:08X} | const VS 0x{:08X} PS 0x{:08X} bool 0x{:08X} "
      "fetch 0x{:08X}",
      e.device, e.const_vs, e.const_ps, e.const_bool, e.fetch_const);
  REXLOG_INFO("ufc3 estado: sujo VS 0x{:016X} PS 0x{:016X} fetch 0x{:016X}", e.sujo_vs, e.sujo_ps,
              e.sujo_fetch);
  REXLOG_INFO("ufc3 estado: fetch constants -- {} slots em uso, {} com endereco plausivel",
              slots_usados, slots_plausiveis);

  if (e.obj_indices) {
    REXLOG_INFO("ufc3 estado: indices obj 0x{:08X} base 0x{:08X} ({} bits)", e.obj_indices,
                e.indices_base, e.indices_32bits ? 32 : 16);
  } else {
    REXLOG_INFO("ufc3 estado: nenhum buffer de indices ligado neste desenho");
  }

  // O veredito, dito sem rodeio: e' isto que decide se da para construir por
  // cima do mapa ou se ele precisa ser refeito.
  if (slots_usados == 0) {
    REXLOG_ERROR(
        "ufc3 estado: MAPA SUSPEITO -- nenhuma fetch constant em uso num desenho. "
        "O deslocamento 0x0480 provavelmente esta errado.");
  } else if (slots_plausiveis == 0) {
    REXLOG_ERROR(
        "ufc3 estado: MAPA SUSPEITO -- {} slots em uso, nenhum com endereco de guest "
        "plausivel. O formato das fetch constants nao e' o que supomos.",
        slots_usados);
  } else {
    REXLOG_INFO(
        "ufc3 estado: MAPA CONFERE -- {} de {} slots de fetch apontam para o guest. "
        "As constantes do jogo podem alimentar o tradutor do SDK direto da sombra.",
        slots_plausiveis, slots_usados);
  }
}

}  // namespace estado_desenho
}  // namespace ufc3
