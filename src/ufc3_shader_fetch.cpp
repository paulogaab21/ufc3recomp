// ufc3 - locating the game's shader microcode
//
// The last link in the chain that lets the native renderer draw. The SDK already
// carries a complete Xenos shader translator -- hand it microcode, get back DXBC
// ready for CreateGraphicsPipelineState. What was missing was knowing where the
// game keeps that microcode, and that is what this file answers.
//
// HOW THE ADDRESS IS BUILT
//
// The shader is not one object with a pointer inside it. There are two levels:
// the shader object itself holds a base address, and a descriptor holds an
// offset into it. The final address is the sum. That indirection exists for a
// reason -- see the note on variants below.
//
// Pixel shader, from P = *(device+0x328C):
//
//     V       = P + *(P + 0x40)          the descriptor
//     address = *(P + 0x18) + *(V + 0x28)
//     dwords  = *(V + 0x2C) >> 2         the field is in bytes
//
// Vertex shader, from S = *(device+0x3290):
//
//     D       = S + *(S + 0x380)         first variant's descriptor
//     address = *(S + 0x20) + *(D + 0x368)
//     dwords  = *(D + 0x36C) >> 2
//
// WHY THE VERTEX SHADER HAS VARIANTS
//
// The runtime patches the shader's own vertex-fetch instructions to match the
// stride actually bound to each stream, and keeps the patched copies as a family
// of descriptors starting at S+0x380, eight bytes apart. The one at +0x380 is
// the initial variant; +0x388 is the alternative.
//
// This reads the first variant. Picking the right one depends on the stream
// state, and the analysis rated that policy medium-high confidence rather than
// high -- so it is deliberately left for when a draw is actually being
// reproduced and the choice can be checked against a real frame, instead of
// guessed here.
//
// AND WHAT THIS REMOVES FROM THE PLAN
//
// There is no vertex declaration object in this game. The vertex layout lives in
// the `vfetch` instructions inside the microcode, so the SDK translator reports
// it after analysing the shader. Only the stride lives outside, in the device.
// That is one whole item off the remaining work, found by looking rather than
// assumed.
//
// Offsets from work/analise-render/achados.md, sections 10 and 11. Two analysis
// passes reached them independently -- one from the disassembly, one from a PM4
// opcode sweep -- which is the strongest guarantee available without running the
// game. And note the correction those sections carry: device+0x328C is the
// PIXEL shader and +0x3290 the VERTEX one, the opposite of the first guess made
// here from the raw dump.

#include "ufc3_shader_fetch.h"

#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>

namespace ufc3 {
namespace shader_fetch {

namespace {

constexpr uint32_t kObjPixelShader  = 0x328C;
constexpr uint32_t kObjVertexShader = 0x3290;

// Inside the pixel shader object and its descriptor.
constexpr uint32_t kPsBase       = 0x18;
constexpr uint32_t kPsDescritor  = 0x40;
constexpr uint32_t kPsOffset     = 0x28;
constexpr uint32_t kPsTamanho    = 0x2C;

// Inside the vertex shader object and its descriptor.
constexpr uint32_t kVsBase       = 0x20;
constexpr uint32_t kVsDescritor  = 0x380;
constexpr uint32_t kVsOffset     = 0x368;
constexpr uint32_t kVsTamanho    = 0x36C;

constexpr uint32_t kGuestMin = 0x00010000u;

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

// Deliberately permissive on the upper bound: the microcode lives in the
// console's physical range, above 0xC0000000, and an earlier version of this
// work missed it entirely by capping "plausible" at 0xE0000000.
inline bool Plausivel(uint32_t a) { return a >= kGuestMin; }

// A shader of a few hundred thousand dwords is not a shader, it is a bad read.
constexpr uint32_t kMaxDwords = 64 * 1024;

Microcodigo Ler(uint8_t* base, uint32_t device, bool pixel) {
  Microcodigo m;
  m.pixel = pixel;
  if (!base || device < kGuestMin) {
    return m;
  }

  m.objeto = LerU32(base, device + (pixel ? kObjPixelShader : kObjVertexShader));
  if (!Plausivel(m.objeto)) {
    return m;
  }

  const uint32_t desc_off = LerU32(base, m.objeto + (pixel ? kPsDescritor : kVsDescritor));
  const uint32_t descritor = m.objeto + desc_off;
  if (!Plausivel(descritor)) {
    return m;
  }

  const uint32_t campo_base = LerU32(base, m.objeto + (pixel ? kPsBase : kVsBase));
  const uint32_t campo_off  = LerU32(base, descritor + (pixel ? kPsOffset : kVsOffset));
  const uint32_t bytes      = LerU32(base, descritor + (pixel ? kPsTamanho : kVsTamanho));

  m.endereco = campo_base + campo_off;
  m.dwords = bytes >> 2;   // the field is in bytes; the translator wants dwords

  m.valido = Plausivel(m.endereco) && m.dwords > 0 && m.dwords <= kMaxDwords;
  return m;
}

std::atomic<bool> g_ja_validou{false};

// Xenos microcode opens with a control-flow header, never with zeros. Reading
// the first words is enough to tell a real pointer from a field that merely
// looked like an address.
void Descrever(uint8_t* base, const Microcodigo& m, const char* rotulo) {
  if (!m.valido) {
    REXLOG_ERROR("ufc3 shader: {} not resolved (object 0x{:08X}, address 0x{:08X}, {} dwords)",
                 rotulo, m.objeto, m.endereco, m.dwords);
    return;
  }
  const uint32_t p0 = LerU32(base, m.endereco);
  const uint32_t p1 = LerU32(base, m.endereco + 4);
  REXLOG_INFO("ufc3 shader: {} object 0x{:08X} -> 0x{:08X}, {} dwords, opens with {:08X} {:08X}{}",
              rotulo, m.objeto, m.endereco, m.dwords, p0, p1,
              (p0 || p1) ? "" : "  -- ALL ZEROS, suspicious");
}

}  // namespace

Microcodigo LerPixelShader(uint8_t* base, uint32_t device) {
  return Ler(base, device, /*pixel=*/true);
}

Microcodigo LerVertexShader(uint8_t* base, uint32_t device) {
  return Ler(base, device, /*pixel=*/false);
}

void ValidarUmaVez(uint8_t* base, uint32_t device) {
  bool esperado = false;
  if (!g_ja_validou.compare_exchange_strong(esperado, true)) {
    return;
  }
  Descrever(base, LerVertexShader(base, device), "vertex");
  Descrever(base, LerPixelShader(base, device), "pixel");
}

}  // namespace shader_fetch
}  // namespace ufc3
