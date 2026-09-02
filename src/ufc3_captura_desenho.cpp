// ufc3 - captura do fluxo de desenho do jogo
//
// Primeiro passo real para o jogo inteiro passar pelo renderizador nativo.
//
// Hoje o desenho percorre este caminho: o jogo chama o D3D9 do XDK, que esta
// LIGADO ESTATICAMENTE dentro do XEX; esse D3D monta pacotes PM4 e os escreve
// no anel de comandos; e o emulador le o anel de volta e reconstroi o quadro
// adivinhando a intencao a partir dos pacotes.
//
// A ultima etapa e a cara, e e' informacao jogada fora e reconstruida: o jogo
// SABIA o que queria desenhar quando chamou o D3D. Para o renderizador nativo
// desenhar direto, o que falta e' pegar essa intencao antes de virar pacote.
//
// Como se engancha: o codegen define cada funcao do jogo com DEFINE_REX_FUNC,
// que cria o simbolo como ALIAS FRACO de __imp__<nome>. Definir o simbolo aqui
// vence o alias, e __imp__<nome> continua sendo a original. E o mesmo mecanismo
// que o skate3recomp usa nas 57 funcoes que substitui.
//
// Este arquivo ainda nao desenha nada: observa. Antes de replicar o fluxo
// nativamente e' preciso saber o tamanho dele -- quantas chamadas por quadro,
// de que tipos de primitiva, com que contagens. Chutar esse numero seria
// desenhar um plano em cima de suposicao.
//
// Enderecos vindos da analise estatica (work/analise-render/achados.md):
//
//   0x82384100  D3DDevice_DrawIndexedVertices(this, tipo, baseVertex,
//                                             startIndex, contagemDeVertices)
//               Emite 0xC0032201 -- tipo-3, 4 dwords, opcode 0x22 DRAW_INDX.
//   0x823832E8  D3DDevice_DrawVertices -- mesmo opcode, 2 dwords, sem indice.
//
// O quarto argumento e contagem de VERTICES, nao de primitivas: os chamadores
// calculam `mul * n + add` pela tabela em 0x82003670. As constantes abaixo
// vieram dessa tabela, lida do binario.

#include "ufc3_captura_desenho.h"
#include "ufc3_estado_desenho.h"
#include "ufc3_extrai_cena.h"

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(ufc3_capturar_desenho, false, "Diagnostico",
                    "Conta as chamadas de desenho do jogo por tipo de primitiva e "
                    "resume no log. Passo de investigacao para o renderizador nativo; "
                    "nao muda a imagem.");

REXCVAR_DEFINE_INT32(ufc3_capturar_desenho_intervalo, 600, "Diagnostico",
                     "De quantas em quantas chamadas de desenho sai um resumo no log.")
    .range(60, 100000);

namespace ufc3 {
namespace captura_desenho {

namespace {

// Tipos de primitiva. A tabela do jogo em 0x82003670 cobre os valores de
// D3DPRIMITIVETYPE (1..8), mas o Xenos tem tipos ALEM do D3D9 de PC, e o UFC 3
// usa: o tipo mais frequente medido em cena foi o 13, que nao existe na tabela
// e por isso saia como "?".
const char* NomeDaPrimitiva(uint32_t tipo) {
  switch (tipo) {
    case 1:  return "pontos";
    case 2:  return "linhas";
    case 3:  return "tira de linhas";
    case 4:  return "triangulos";
    case 5:  return "tira de triangulos";
    case 6:  return "leque de triangulos";
    case 7:  return "triangulos com flag W";
    case 8:  return "retangulos";
    case 12: return "laco de linhas";
    case 13: return "quads";
    case 14: return "tira de quads";
    case 15: return "poligono";
    default: return "?";
  }
}

constexpr uint32_t kMaxTipo = 16;

struct Contadores {
  std::atomic<uint64_t> chamadas{0};
  std::atomic<uint64_t> vertices{0};
  std::atomic<uint64_t> por_tipo[kMaxTipo] = {};
  std::atomic<uint64_t> nao_indexadas{0};
  // Maior contagem de vertices vista numa unica chamada: e' o que dimensiona os
  // buffers que o renderizador nativo vai precisar montar.
  std::atomic<uint32_t> maior_lote{0};
};

Contadores g_c;

void Registrar(uint32_t tipo, uint32_t vertices, bool indexada) {
  g_c.vertices.fetch_add(vertices, std::memory_order_relaxed);
  if (tipo < kMaxTipo) {
    g_c.por_tipo[tipo].fetch_add(1, std::memory_order_relaxed);
  }
  if (!indexada) {
    g_c.nao_indexadas.fetch_add(1, std::memory_order_relaxed);
  }

  uint32_t maior = g_c.maior_lote.load(std::memory_order_relaxed);
  while (vertices > maior &&
         !g_c.maior_lote.compare_exchange_weak(maior, vertices, std::memory_order_relaxed)) {
  }

  const uint64_t n = g_c.chamadas.fetch_add(1, std::memory_order_relaxed) + 1;
  const uint32_t intervalo = uint32_t(REXCVAR_GET(ufc3_capturar_desenho_intervalo));
  if (intervalo == 0 || (n % intervalo) != 0) {
    return;
  }

  // Resumo. Sai a cada N chamadas em vez de por quadro porque nao ha, deste
  // lado, um sinal confiavel de fim de quadro -- e o que interessa agora e' a
  // proporcao entre os tipos, nao o instante.
  std::string linha;
  for (uint32_t t = 0; t < kMaxTipo; ++t) {
    const uint64_t v = g_c.por_tipo[t].load(std::memory_order_relaxed);
    if (!v) continue;
    // Tipo fora da tabela sai com o numero: agrupar tudo em "?" esconde
    // justamente o caso que precisa ser investigado.
    const char* nome = NomeDaPrimitiva(t);
    if (nome[0] == '?') {
      linha += fmt::format("tipo{}={} ", t, v);
    } else {
      linha += fmt::format("{}={} ", nome, v);
    }
  }
  REXLOG_INFO(
      "ufc3 desenho: {} chamadas, {} vertices, maior lote {}, {} nao indexadas | {}",
      n, g_c.vertices.load(std::memory_order_relaxed),
      g_c.maior_lote.load(std::memory_order_relaxed),
      g_c.nao_indexadas.load(std::memory_order_relaxed), linha);
}

}  // namespace

void ResumirAgora() {
  REXLOG_INFO("ufc3 desenho: total {} chamadas, {} vertices, maior lote {}",
              g_c.chamadas.load(std::memory_order_relaxed),
              g_c.vertices.load(std::memory_order_relaxed),
              g_c.maior_lote.load(std::memory_order_relaxed));
}

}  // namespace captura_desenho
}  // namespace ufc3

// ---------------------------------------------------------------------------
//  Os ganchos
//
//  Definir estes simbolos substitui o alias fraco que o codegen criou. A
//  original continua acessivel por __imp__, e e' sempre chamada: enquanto isto
//  for so observacao, o jogo tem de desenhar exatamente como desenhava.
// ---------------------------------------------------------------------------

extern "C" REX_FUNC(__imp__sub_82384100);
extern "C" REX_FUNC(__imp__sub_823832E8);

// D3DDevice_DrawIndexedVertices(this r3, tipo r4, baseVertex r5,
//                               startIndex r6, contagemDeVertices r7)
extern "C" REX_FUNC(sub_82384100) {
  if (REXCVAR_GET(ufc3_capturar_desenho)) {
    ufc3::captura_desenho::Registrar(ctx.r4.u32, ctx.r7.u32, /*indexada=*/true);
  }
  // r3 e o D3DDevice. Aqui, num desenho de verdade, e o unico lugar onde temos
  // device valido em maos junto com a memoria do guest -- e o momento certo de
  // conferir se o mapa de deslocamentos bate com a realidade. Custa uma vez.
  ufc3::estado_desenho::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::extrai_cena::Observar(base, ctx.r3.u32, ctx.r4.u32, ctx.r7.u32,
                              int32_t(ctx.r5.u32), ctx.r6.u32, /*indexada=*/true);
  __imp__sub_82384100(ctx, base);
}

// D3DDevice_DrawVertices(this r3, tipo r4, startVertex r5, contagem r6)
extern "C" REX_FUNC(sub_823832E8) {
  if (REXCVAR_GET(ufc3_capturar_desenho)) {
    ufc3::captura_desenho::Registrar(ctx.r4.u32, ctx.r6.u32, /*indexada=*/false);
  }
  // Tambem daqui: nos menus o jogo so usa o caminho NAO indexado, e a validacao
  // presa ao outro gancho simplesmente nunca rodava. O device e r3 nos dois.
  ufc3::estado_desenho::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::extrai_cena::Observar(base, ctx.r3.u32, ctx.r4.u32, ctx.r6.u32,
                              int32_t(ctx.r5.u32), 0, /*indexada=*/false);
  __imp__sub_823832E8(ctx, base);
}
