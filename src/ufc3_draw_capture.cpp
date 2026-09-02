// ufc3 - capture of the game's draw stream
//
// The first real step toward the whole game going through the native renderer.
//
// Today drawing takes this path: the game calls the XDK's D3D9, which is
// STATICALLY LINKED inside the XEX; that D3D builds PM4 packets and writes them
// into the command ring; and the emulator reads the ring back and rebuilds the
// frame by guessing the intent from the packets.
//
// That last step is the expensive one, and it is information thrown away and
// then reconstructed: the game KNEW what it wanted to draw when it called D3D.
// For the native renderer to draw directly, what is missing is catching that
// intent before it turns into packets.
//
// How the hook works: codegen defines every game function with DEFINE_REX_FUNC,
// which creates the symbol as a WEAK ALIAS of __imp__<name>. Defining the symbol
// here beats the alias, and __imp__<name> remains the original. It is the same
// mechanism skate3recomp uses on the 57 functions it replaces.
//
// This file does not draw anything yet: it observes. Before replicating the
// stream natively you have to know its size -- how many calls per frame, of
// which primitive types, with what counts. Guessing that number would mean
// drawing a plan on top of an assumption.
//
// Addresses from the static analysis (work/analise-render/achados.md):
//
//   0x82384100  D3DDevice_DrawIndexedVertices(this, type, baseVertex,
//                                             startIndex, vertexCount)
//               Emits 0xC0032201 -- type-3, 4 dwords, opcode 0x22 DRAW_INDX.
//   0x823832E8  D3DDevice_DrawVertices -- same opcode, 2 dwords, no index.
//
// The fourth argument is a VERTEX count, not a primitive count: callers compute
// `mul * n + add` from the table at 0x82003670. The constants below came from
// that table, read out of the binary.

#include "ufc3_draw_capture.h"
#include "ufc3_draw_state.h"
#include "ufc3_scene_extract.h"
#include "ufc3_shader_fetch.h"

#include <atomic>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

REXCVAR_DEFINE_BOOL(ufc3_capturar_desenho, false, "Diagnostics",
                    "Counts the game's draw calls by primitive type and summarises them "
                    "in the log. An investigation step for the native renderer; it does "
                    "not change the image.");

REXCVAR_DEFINE_INT32(ufc3_capturar_desenho_intervalo, 600, "Diagnostics",
                     "How many draw calls between summaries in the log.")
    .range(60, 100000);

namespace ufc3 {
namespace captura_desenho {

namespace {

// Primitive types. The game's table at 0x82003670 covers the D3DPRIMITIVETYPE
// values (1..8), but Xenos has types BEYOND desktop D3D9, and UFC 3 uses them:
// the most frequent type measured in a scene was 13, which is not in the table
// and therefore used to come out as "?".
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
  // Largest vertex count seen in a single call: it is what sizes the buffers the
  // native renderer will need to build.
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

  // Summary. It comes out every N calls rather than per frame because there is
  // no reliable end-of-frame signal on this side -- and what matters right now
  // is the ratio between types, not the exact moment.
  std::string linha;
  for (uint32_t t = 0; t < kMaxTipo; ++t) {
    const uint64_t v = g_c.por_tipo[t].load(std::memory_order_relaxed);
    if (!v) continue;
    // A type outside the table comes out with its number: lumping everything
    // into "?" hides exactly the case that needs investigating.
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
//  The hooks
//
//  Defining these symbols replaces the weak alias codegen created. The original
//  stays reachable through __imp__, and is always called: while this is only
//  observation, the game must draw exactly as it drew before.
// ---------------------------------------------------------------------------

extern "C" REX_FUNC(__imp__sub_82384100);
extern "C" REX_FUNC(__imp__sub_823832E8);

// D3DDevice_DrawIndexedVertices(this r3, type r4, baseVertex r5,
//                               startIndex r6, vertexCount r7)
extern "C" REX_FUNC(sub_82384100) {
  if (REXCVAR_GET(ufc3_capturar_desenho)) {
    ufc3::captura_desenho::Registrar(ctx.r4.u32, ctx.r7.u32, /*indexada=*/true);
  }
  // r3 is the D3DDevice. Here, inside a real draw, is the only place where a
  // valid device pointer and guest memory are both in hand -- the right moment
  // to check whether the offset map matches reality. It costs one call.
  ufc3::estado_desenho::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::shader_fetch::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::extrai_cena::Observar(base, ctx.r3.u32, ctx.r4.u32, ctx.r7.u32,
                              int32_t(ctx.r5.u32), ctx.r6.u32, /*indexada=*/true);
  __imp__sub_82384100(ctx, base);
}

// D3DDevice_DrawVertices(this r3, type r4, startVertex r5, count r6)
extern "C" REX_FUNC(sub_823832E8) {
  if (REXCVAR_GET(ufc3_capturar_desenho)) {
    ufc3::captura_desenho::Registrar(ctx.r4.u32, ctx.r6.u32, /*indexada=*/false);
  }
  // From here too: in menus the game only uses the NON-indexed path, and
  // validation tied to the other hook simply never ran. The device is r3 in
  // both.
  ufc3::estado_desenho::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::shader_fetch::ValidarUmaVez(base, ctx.r3.u32);
  ufc3::extrai_cena::Observar(base, ctx.r3.u32, ctx.r4.u32, ctx.r6.u32,
                              int32_t(ctx.r5.u32), 0, /*indexada=*/false);
  __imp__sub_823832E8(ctx, base);
}
