#pragma once

// Renderizador nativo do ufc3. Ver ufc3_render_nativo.cpp para o porque do
// desenho nativo e como ele convive com a emulacao.

namespace ufc3 {
namespace render_nativo {

// Registra os dois callbacks no SDK. Chamar uma vez, na subida do app, antes do
// primeiro quadro. Registrar por si nao muda nada: enquanto o renderizador
// ceder todos os quadros, o jogo desenha por emulacao como antes.
void Registrar();

// Avisa que o menu de configuracoes abriu ou fechou. O fundo desfocado
// acompanha esse estado com um desvanecimento.
void AoAbrirFecharMenu(bool aberto);

// Avanca o desvanecimento. Chamar uma vez por quadro, com o tempo decorrido.
void AvancarQuadro(float segundos);

}  // namespace render_nativo
}  // namespace ufc3
