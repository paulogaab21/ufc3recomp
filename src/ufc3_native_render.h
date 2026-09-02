#pragma once

// UFC 3 native renderer. See ufc3_native_render.cpp for why native drawing
// exists and how it coexists with emulation.

namespace ufc3 {
namespace render_nativo {

// Registers both SDK callbacks. Call once at startup, before the first frame.
// Registering by itself changes nothing: while the renderer yields every frame,
// the game draws through emulation exactly as before.
void Registrar();

// Tells the renderer the settings menu opened or closed. The blurred backdrop
// follows that state with a fade.
void AoAbrirFecharMenu(bool aberto);

// Advances the fade. Call once per frame, with the elapsed time.
void AvancarQuadro(float segundos);

}  // namespace render_nativo
}  // namespace ufc3
