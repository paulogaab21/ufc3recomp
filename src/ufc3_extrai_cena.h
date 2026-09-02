#pragma once

#include <cstdint>

// Extracao de cena: le do jogo o que ele ia desenhar, para o renderizador
// nativo poder desenhar no lugar dele.
//
// Este e o caminho do skate3recomp: em vez de traduzir os pacotes que o D3D do
// jogo produz, lemos as estruturas do proprio jogo e desenhamos com sombreamento
// novo. E' o que abre MSAA, sombras suaves e oclusao ambiente -- coisas que a
// emulacao nao pode dar, porque ela e' fiel ao que o console fazia.
//
// Ver ufc3_extrai_cena.cpp.

namespace ufc3 {
namespace extrai_cena {

// Uma chamada de desenho, como o jogo a descreveu.
struct Desenho {
  uint32_t tipo_primitiva = 0;
  uint32_t contagem_vertices = 0;
  int32_t  base_vertice = 0;
  uint32_t indice_inicial = 0;
  bool     indexada = false;

  uint32_t indices_base = 0;      // endereco no guest
  bool     indices_32bits = false;

  // Fetch constants cruas, do jeito que o jogo as mantem. Sao a chave da
  // geometria: e' delas que saem os enderecos dos buffers de vertice.
  uint32_t fetch[32 * 6] = {};
  uint32_t fetch_usadas = 0;
};

// Chamar de dentro do gancho de desenho. Enquanto a captura nao estiver armada,
// custa uma leitura de bandeira.
void Observar(uint8_t* base, uint32_t device, uint32_t tipo, uint32_t contagem,
              int32_t base_vertice, uint32_t indice_inicial, bool indexada);

}  // namespace extrai_cena
}  // namespace ufc3
