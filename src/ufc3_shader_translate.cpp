// ufc3 - translating the game's shaders into DXBC
//
// NOT BUILT YET -- and the reason is architectural, worth recording.
//
// This file compiles but does not link into the game executable. The Xenos
// shader translator is compiled into the GPU PLUGIN (rexgpu-xenos), and the
// runtime the executable links carries no graphics code at all. So the game
// side cannot call the translator directly, no matter where the call sits.
//
// skate3recomp never hit this: in rexglue 0.8.0 the graphics were compiled
// straight into the executable, with no plugin. The 0.10.0 split into a
// dlopen'd GPU plugin is what moves the boundary.
//
// The consequence for the native renderer is concrete: the scene-drawing code
// has to live on the GPU side, not in the game executable. Reading the game's
// structures still works from there -- the plugin has the same guest memory --
// so this is a question of placement, not of possibility.
//
// The code below stays because it is correct and was verified against the
// prototype; it is waiting on that move, not on a fix.
//
// This closes the chain. Everything before it was finding things; this is the
// first step that produces something the PC can actually run:
//
//   draw hook -> device -> shader object -> descriptor -> microcode
//                                                            |
//                                          SDK Xenos translator
//                                                            |
//                                                          DXBC
//
// The translator is the SDK's own, the one the emulated path already uses in
// production. That matters more than it sounds: skate3recomp ported Skate 3's
// material shading to HLSL by hand, and that was the bulk of its ~30,000 lines.
// Reusing the translator removes that work from this project entirely.
//
// WHAT THE PRODUCED DXBC IS NOT
//
// It is not a self-contained PC shader. It expects the Xenos ABI: constant
// buffers for float, boolean, loop and fetch constants, system constants,
// textures and samplers at the bindings it declares, and -- the part that
// surprises people -- the guest's physical memory bound as a ByteAddressBuffer,
// because the translated vertex shader performs its own `vfetch` there instead
// of using the input assembler.
//
// So this file is not "one call away from drawing". It proves the game's real
// shaders translate, and it reports what each one needs. Supplying that state is
// the next piece of work, and it is why the native renderer still yields every
// frame.
//
// WHY TRANSLATE ONCE PER HASH
//
// A scene issues hundreds of draws per frame but uses a handful of distinct
// shaders. Keying on the microcode hash means the log shows those few, and
// repeated draws cost one hash lookup instead of a full translation.

#include "ufc3_shader_translate.h"

#include <bit>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

#include <rex/cvar.h>
#include <rex/graphics/pipeline/shader/dxbc.h>
#include <rex/graphics/pipeline/shader/dxbc_translator.h>
#include <rex/logging.h>
#include <rex/string/buffer.h>
#include <rex/ui/graphics_provider.h>

#include "ufc3_shader_fetch.h"

REXCVAR_DEFINE_BOOL(ufc3_traduzir_shaders, false, "Diagnostics",
                    "Translates the game's shaders to DXBC as they are used, and reports "
                    "each distinct one. A step toward native scene rendering; it does not "
                    "change the image.");

namespace ufc3 {
namespace shader_translate {

namespace {

using rex::graphics::DxbcShader;
using rex::graphics::DxbcShaderTranslator;
using rex::graphics::Shader;
using rex::graphics::xenos::ShaderType;

inline uint32_t DeslocamentoFisico(uint32_t endereco) {
#if defined(_WIN32)
  return endereco >= 0xE0000000u ? 0x1000u : 0u;
#else
  (void)endereco;
  return 0u;
#endif
}

// FNV-1a. The hash only has to distinguish shaders from each other, so the
// cheapest thing that does that is the right choice.
uint64_t Hash(const uint8_t* dados, size_t n) {
  uint64_t h = 0xCBF29CE484222325ull;
  for (size_t i = 0; i < n; ++i) {
    h = (h ^ dados[i]) * 0x100000001B3ull;
  }
  return h;
}

std::mutex g_mutex;
std::unordered_map<uint64_t, bool> g_vistos;   // hash -> translated successfully
uint64_t g_traduzidos = 0;
uint64_t g_falharam = 0;

void Traduzir(uint8_t* base, const shader_fetch::Microcodigo& m, const char* rotulo) {
  if (!m.valido) {
    return;
  }

  // The microcode lives in guest memory in big-endian, the way the console left
  // it. Copy it out first: the translator wants a contiguous buffer, and the
  // guest range can move under a long translation.
  std::vector<uint32_t> ucode(m.dwords);
  std::memcpy(ucode.data(), base + m.endereco + DeslocamentoFisico(m.endereco),
              size_t(m.dwords) * sizeof(uint32_t));

  const uint64_t hash =
      Hash(reinterpret_cast<const uint8_t*>(ucode.data()), ucode.size() * sizeof(uint32_t));

  {
    std::lock_guard<std::mutex> trava(g_mutex);
    if (g_vistos.find(hash) != g_vistos.end()) {
      return;
    }
    g_vistos.emplace(hash, false);
  }

  const ShaderType tipo = m.pixel ? ShaderType::kPixel : ShaderType::kVertex;

  // std::endian::big tells the constructor the dwords still need swapping; it
  // converts them once, here, rather than on every read later.
  DxbcShader shader(tipo, hash, ucode.data(), ucode.size(), std::endian::big);
  rex::string::StringBuffer disassembly;
  shader.AnalyzeUcode(disassembly);

  // Bindful resources, native render targets, no ROV: the simplest profile the
  // D3D12 backend accepts, and the one a native renderer would use.
  DxbcShaderTranslator translator(rex::ui::GraphicsProvider::GpuVendorID::kAMD,
                                  /*bindless_resources_used=*/false,
                                  /*edram_rov_used=*/false);

  const uint32_t regs = shader.GetDynamicAddressableRegisterCount(0);
  const uint64_t modificacao =
      m.pixel ? translator.GetDefaultPixelShaderModification(regs)
              : translator.GetDefaultVertexShaderModification(
                    regs, Shader::HostVertexShaderType::kVertex);

  Shader::Translation* traducao = shader.GetOrCreateTranslation(modificacao);
  const bool ok = translator.TranslateAnalyzedShader(*traducao) && traducao->is_valid();

  std::lock_guard<std::mutex> trava(g_mutex);
  if (!ok) {
    ++g_falharam;
    g_vistos[hash] = false;
    REXLOG_ERROR("ufc3 translate: {} 0x{:016X} FAILED ({} dwords)", rotulo, hash, m.dwords);
    for (const Shader::Error& e : traducao->errors()) {
      REXLOG_ERROR("ufc3 translate:   {}{}", e.is_fatal ? "fatal: " : "warning: ", e.message);
    }
    return;
  }

  ++g_traduzidos;
  g_vistos[hash] = true;

  const std::vector<uint8_t>& dxbc = traducao->translated_binary();
  const bool assinatura = dxbc.size() >= 4 && std::memcmp(dxbc.data(), "DXBC", 4) == 0;

  // The binding counts say how much state a native draw would have to supply, so
  // they are worth more in the log than the byte size alone.
  //
  // vertex_bindings() is the interesting one: it is the vertex layout the
  // translator recovered from the shader's own `vfetch` instructions. The static
  // analysis found no vertex declaration object anywhere in the game, and this
  // is why -- the layout was never stored separately, it was always in the
  // shader. So the number below is a structure we would otherwise have had to
  // reverse engineer, handed over for free.
  REXLOG_INFO(
      "ufc3 translate: {} 0x{:016X} -- {} dwords -> {} bytes of DXBC {} | "
      "{} vertex bindings, {} texture bindings",
      rotulo, hash, m.dwords, dxbc.size(), assinatura ? "ok" : "WITHOUT SIGNATURE",
      shader.vertex_bindings().size(), shader.texture_bindings().size());
}

}  // namespace

void TraduzirUmaVez(uint8_t* base, uint32_t device) {
  if (!REXCVAR_GET(ufc3_traduzir_shaders) || !base) {
    return;
  }
  Traduzir(base, shader_fetch::LerVertexShader(base, device), "vertex");
  Traduzir(base, shader_fetch::LerPixelShader(base, device), "pixel");
}

void Resumo(uint64_t& traduzidos, uint64_t& falharam) {
  std::lock_guard<std::mutex> trava(g_mutex);
  traduzidos = g_traduzidos;
  falharam = g_falharam;
}

}  // namespace shader_translate
}  // namespace ufc3
