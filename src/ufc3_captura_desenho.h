#pragma once

// Captura do fluxo de desenho do jogo. Ver ufc3_captura_desenho.cpp para o
// porque e para como o gancho substitui a funcao do codegen.

namespace ufc3 {
namespace captura_desenho {

// Despeja o total acumulado no log. Os ganchos ja resumem periodicamente;
// isto serve para fechar a conta num momento escolhido.
void ResumirAgora();

}  // namespace captura_desenho
}  // namespace ufc3
