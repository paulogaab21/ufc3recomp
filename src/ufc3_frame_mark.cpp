// ufc3 - frame boundary, in the game's own terms
//
// WHY THIS MATTERS
//
// The native renderer has to gather one frame's draws and issue them together.
// Until now the draw hooks could only count calls, with no idea where one frame
// ended and the next began -- which is why every measurement so far was "157
// thousand calls per session" rather than the number that actually sizes the
// work: calls per frame.
//
// WHERE THE BOUNDARY IS
//
// 0x82747750, called from exactly one place (0x8236A840, inside the game
// thread's frame function). What it does, in order: takes the command manager's
// critical section, walks every command list appending a terminator and
// invalidating its state cache so the next frame re-emits everything, then
// flips two parity bits:
//
//     [mgr+0x28] = ([mgr+0x28] + 1) & 1
//     [mgr+0x2C] = ([mgr+0x2C] + 1) & 1
//
// That flip is the frame boundary. The two `clrlwi ...,31` instructions are
// unambiguous, and the two fields flipped are exactly the ones the append path
// and the interpreter consult.
//
// There is NO BeginScene equivalent. The frame is not opened -- it begins when
// the parity flips and the write buffer comes up empty. So the end is the only
// real marker, and "start of frame" is whatever runs after it.
//
// TWO THREADS DRAW
//
// The architecture is producer/consumer with double buffering: the game thread
// fills one buffer while the render thread plays back the other, and they
// synchronise through an object at [0x833DF0C0]. This hook sits on the game
// thread's boundary, which is the one that defines what belongs to a frame.
//
// The hook runs BEFORE the original, on purpose: at that instant the write
// buffer still holds the frame that just finished. After the original returns,
// the parity has flipped and that buffer is the next frame's.
//
// Addresses and the disassembly behind them: work/analise-quadro/quadro.md.

#include "ufc3_frame_mark.h"

#include <atomic>
#include <cstring>

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ppc/context.h>
#include <rex/ppc/func.h>

#include "ufc3_draw_capture.h"

REXCVAR_DEFINE_INT32(ufc3_frames_log, 0, "Diagnostics",
                     "Logs draw statistics every N frames, counted on the game's own "
                     "frame boundary. 0 turns it off. Suggested: 120.")
    .range(0, 10000);

namespace ufc3 {
namespace frame_mark {

namespace {

// The command manager singleton. The frame counter the game keeps lives at
// +0x30 inside it; this file keeps its own count instead, because ours starts at
// process start and cannot be reset by the game underneath us.
constexpr uint32_t kGerenciador = 0x833C80B4;
constexpr uint32_t kParidadeEscrita = 0x28;

std::atomic<uint64_t> g_quadros{0};
std::atomic<uint32_t> g_paridade_anterior{0xFFFFFFFF};
std::atomic<uint64_t> g_paridade_repetida{0};

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

}  // namespace

uint64_t Contagem() { return g_quadros.load(std::memory_order_relaxed); }

void AoFecharQuadro(uint8_t* base) {
  const uint64_t n = g_quadros.fetch_add(1, std::memory_order_relaxed) + 1;

  // The parity is what proves this hook really sits on the frame boundary: it
  // has to alternate on CONSECUTIVE frames. So it is sampled every frame and
  // compared with the previous one -- reading it only at the log interval was
  // useless, because an even interval always lands on the same parity and the
  // check looked constant no matter what.
  uint32_t paridade = 0xFFFFFFFF;
  const uint32_t mgr = LerU32(base, kGerenciador);
  if (mgr >= 0x00010000u) {
    paridade = LerU32(base, mgr + kParidadeEscrita);
  }
  if (paridade != 0xFFFFFFFF) {
    const uint32_t anterior = g_paridade_anterior.exchange(paridade, std::memory_order_relaxed);
    if (anterior != 0xFFFFFFFF && anterior == paridade) {
      g_paridade_repetida.fetch_add(1, std::memory_order_relaxed);
    }
  }

  const int intervalo = REXCVAR_GET(ufc3_frames_log);
  if (intervalo <= 0 || (n % uint64_t(intervalo)) != 0) {
    return;
  }

  const uint64_t repetidas = g_paridade_repetida.load(std::memory_order_relaxed);
  REXLOG_INFO("ufc3 frame: {} frames | parity {} | did not alternate {} times{}", n, paridade,
              repetidas, repetidas ? "  -- SUSPECT, hook may be in the wrong place" : "");
  captura_desenho::ResumirAgora();
}

}  // namespace frame_mark
}  // namespace ufc3

// ---------------------------------------------------------------------------
//  The hook
//
//  Runs before the original because at this instant the write buffer still
//  holds the frame that just finished; once the original returns, the parity has
//  flipped and that buffer belongs to the next frame.
// ---------------------------------------------------------------------------

extern "C" REX_FUNC(__imp__sub_82747750);

extern "C" REX_FUNC(sub_82747750) {
  ufc3::frame_mark::AoFecharQuadro(base);
  __imp__sub_82747750(ctx, base);
}
