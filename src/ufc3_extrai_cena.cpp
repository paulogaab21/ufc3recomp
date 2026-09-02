// ufc3 - extracao de cena
//
// O renderizador nativo do skate3recomp nao traduz o que o jogo mandou para a
// GPU: ele le as estruturas do proprio jogo e desenha com sombreamento novo. E'
// dai que vieram os ganhos deles -- mais que o dobro dos quadros a um quarto do
// consumo, com MSAA e sombras suaves que a emulacao nao pode dar, porque a
// emulacao e' fiel ao que o console fazia.
//
// Este arquivo comeca esse caminho pela unica ponta que se pode comecar: ver o
// que existe. Antes de desenhar geometria do jogo e' preciso PROVAR que se
// consegue localiza-la, e a prova precisa ser dado bruto, nao suposicao.
//
// POR QUE ESTE ARQUIVO SO DESPEJA
//
// As fetch constants do Xenos guardam geometria e texturas no mesmo vetor de 32
// slots de 6 dwords, e nao ha como separar um do outro olhando o vetor. Tentei
// separar pelo campo de tipo nos 2 bits baixos: um slot de textura ZERADO tem o
// padrao 00A80000 00000003 00000000, e aquele 00000003 passa por "tipo 3 =
// vertice". Saíram 3.150 falsos fetches de vertice num despejo de 150 desenhos.
//
// Quem sabe quais slots sao de vertice e quais de textura e o SHADER, pelas
// instrucoes vfetch/tfetch. Por isso o caminho obrigatorio e: microcodigo ->
// tradutor do SDK -> lista de bindings -> so entao decodificar. Enquanto o
// microcodigo nao estiver localizado, decodificar aqui e' chute com aparencia
// de dado.
//
// Entao o que se faz aqui e' guardar o cru com o contexto do desenho ao lado.
// Com o arquivo em maos da para conferir de fora coisas que o jogo correndo nao
// mostra: um mesmo slot repetido entre desenhos do mesmo objeto, um tamanho
// coerente com a contagem de vertices.
//
// O que se sabe ate aqui, medido em cena (nao suposto):
//   157.200 chamadas de desenho, 18,5 milhoes de vertices
//   maior lote 4.577 vertices; nos menus, 24
//   142.651 de 157.200 sao NAO indexadas
//   tipo 13 domina com 94.751 -- e nao existe na tabela do jogo em 0x82003670
//
// Esse tipo 13 e a razao de o despejo guardar o tipo cru: se ele for mesmo lista
// de quads, o renderizador nativo vai precisar converter para triangulos, e a
// conversao muda a contagem de indices.

#include "ufc3_extrai_cena.h"

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include <rex/cvar.h>
#include <rex/filesystem.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(ufc3_extrair_cena, 0, "Diagnostics",
                     "Dumps the description of the next N draw calls to a file, for study "
                     "outside the game. 0 turns it off. Suggested: 200.")
    .range(0, 20000);

namespace ufc3 {
namespace extrai_cena {

namespace {

constexpr uint32_t kSujoFetch  = 0x0018;
constexpr uint32_t kFetchConst = 0x0480;
constexpr uint32_t kObjIndices = 0x318C;

// A analise estatica achou 0x82377BF8 emitindo o pacote 0xC0006200 -- IM_LOAD,
// que e' como o Xenos recebe microcodigo de shader -- usando [device+0x329C].
// Se os ponteiros para o microcodigo moram em algum lugar, e' por ali. Uma
// janela ao redor custa pouco e pode fechar o unico gargalo que resta.
constexpr uint32_t kJanelaShader = 0x3280;
constexpr uint32_t kJanelaShaderDwords = 24;  // 0x3280..0x32DC
constexpr uint32_t kIdxFormato = 0x00;
constexpr uint32_t kIdxBase    = 0x18;
constexpr uint32_t kIdx32Bits  = 0x80000000u;

constexpr uint32_t kGuestMin = 0x00010000u;
constexpr uint32_t kGuestMax = 0xE0000000u;

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

inline bool Plausivel(uint32_t a) { return a >= kGuestMin && a < kGuestMax; }

std::mutex g_mutex;
FILE* g_arquivo = nullptr;
uint32_t g_restantes = 0;
std::atomic<bool> g_armado{false};
std::atomic<bool> g_terminado{false};

void AbrirSeNecessario() {
  if (g_arquivo) {
    return;
  }
  const auto caminho = rex::filesystem::GetExecutableFolder() / "cena-ufc3.txt";
  g_arquivo = rex::filesystem::OpenFile(caminho, "w");
  if (!g_arquivo) {
    REXLOG_ERROR("ufc3 cena: nao consegui abrir {}", caminho.string());
    return;
  }
  REXLOG_INFO("ufc3 cena: despejando em {}", caminho.string());
  std::fprintf(g_arquivo,
               "# Descricao das chamadas de desenho do UFC 3, lida do D3DDevice do jogo.\n"
               "# fetch constants em bruto: dois formatos convivem no mesmo vetor -- um\n"
               "# vertex fetch ocupa 2 dwords, um texture fetch ocupa 6. Confira de fora.\n\n");
}

void Fechar() {
  if (g_arquivo) {
    std::fclose(g_arquivo);
    g_arquivo = nullptr;
  }
}

// Abre o objeto de shader e mostra os primeiros dwords, CRUS.
//
// Os dois ponteiros em device+0x328C e +0x3290 apontam para estruturas cuja
// primeira palavra e 0x00100007 e 0x00100006 -- iguais menos por um bit. E' a
// assinatura de um par vertex/pixel shader, com o tipo no campo baixo.
//
// Uma versao anterior tentava seguir cada palavra que parecesse endereco, para
// dizer qual apontava para o microcodigo. Isso DERRUBOU o jogo: a maior parte da
// faixa que eu chamava de "plausivel" nao esta mapeada, e o acesso invalido
// matava o processo antes de escrever qualquer coisa -- o arquivo saia com zero
// bytes e o motivo nao aparecia em lugar nenhum.
//
// Seguir ponteiro as cegas na memoria do guest exige leitura protegida, que e' o
// que o skate3recomp resolveu com GuestTryCopy. Enquanto nao houver isso aqui,
// mostrar o cru basta: da para reconhecer o par (endereco, tamanho) olhando o
// arquivo, sem arriscar o processo.
void AbrirObjetoDeShader(uint8_t* base, const char* rotulo, uint32_t obj) {
  if (!Plausivel(obj)) {
    return;
  }
  std::fprintf(g_arquivo, "  %s 0x%08X:", rotulo, obj);
  for (uint32_t i = 0; i < 20; ++i) {
    std::fprintf(g_arquivo, " %08X", LerU32(base, obj + i * 4));
  }
  std::fprintf(g_arquivo, "\n");

  // Os campos +0x18 e +0x20 sao os candidatos a ponteiro de microcodigo: cada
  // objeto usa um dos dois e zera o outro. Os valores caem em 0xF427x000 --
  // alinhados a pagina, na faixa FISICA do console. Estavam invisiveis porque o
  // teste de "plausivel" cortava acima de 0xE0000000, faixa que o proprio
  // runtime ja trata a parte (ver DeslocamentoFisico).
  //
  // Microcodigo do Xenos comeca por um cabecalho de controle de fluxo, nao por
  // zeros. Ler as primeiras palavras confirma ou desmente de uma vez -- e o
  // endereco vem do proprio objeto, nao de um palpite sobre a memoria.
  for (const uint32_t off : {0x18u, 0x20u}) {
    const uint32_t ptr = LerU32(base, obj + off);
    if (ptr < 0xC0000000u) {
      continue;
    }
    std::fprintf(g_arquivo, "    %s+0x%02X -> 0x%08X microcodigo:", rotulo, off, ptr);
    for (uint32_t i = 0; i < 8; ++i) {
      std::fprintf(g_arquivo, " %08X", LerU32(base, ptr + i * 4));
    }
    std::fprintf(g_arquivo, "\n");
  }
}

}  // namespace

void Observar(uint8_t* base, uint32_t device, uint32_t tipo, uint32_t contagem,
              int32_t base_vertice, uint32_t indice_inicial, bool indexada) {
  // Porteira sem trava, antes de tudo. O despejo e' curto -- dezenas de desenhos
  // -- mas o gancho continua sendo chamado ~150 mil vezes por sessao. A primeira
  // versao pegava o mutex global em TODAS elas, inclusive muito depois de o
  // despejo ter acabado, e o jogo ficava visivelmente lento. Uma leitura atomica
  // custa nada e corta o caminho inteiro.
  if (g_terminado.load(std::memory_order_relaxed)) {
    return;
  }
  const int pedidos = REXCVAR_GET(ufc3_extrair_cena);
  if (pedidos <= 0) {
    return;
  }
  if (!base || !Plausivel(device)) {
    return;
  }

  std::lock_guard<std::mutex> trava(g_mutex);
  if (!g_armado.exchange(true)) {
    g_restantes = uint32_t(pedidos);
    AbrirSeNecessario();
  }
  if (!g_restantes || !g_arquivo) {
    return;
  }
  --g_restantes;

  const uint32_t fetch = device + kFetchConst;
  const uint32_t obj_idx = LerU32(base, device + kObjIndices);
  uint32_t idx_base = 0;
  bool idx32 = false;
  if (Plausivel(obj_idx)) {
    idx_base = LerU32(base, obj_idx + kIdxBase);
    idx32 = (LerU32(base, obj_idx + kIdxFormato) & kIdx32Bits) != 0;
  }

  std::fprintf(g_arquivo,
               "desenho tipo=%u vertices=%u baseVert=%d idxInicial=%u %s\n", tipo, contagem,
               base_vertice, indice_inicial, indexada ? "indexada" : "nao-indexada");
  if (indexada) {
    std::fprintf(g_arquivo, "  indices obj=0x%08X base=0x%08X %u bits\n", obj_idx, idx_base,
                 idx32 ? 32u : 16u);
  }
  std::fprintf(g_arquivo, "  sujo_fetch=0x%08X%08X\n", LerU32(base, device + kSujoFetch),
               LerU32(base, device + kSujoFetch + 4));

  // O vetor tem 32 slots de 6 dwords. Um fetch de TEXTURA ocupa o slot inteiro;
  // um de VERTICE ocupa 2 dwords, entao cabem tres por slot.
  //
  // NAO DA PARA SABER QUAL E QUAL VARRENDO O VETOR. Tentei: o campo de tipo nos
  // 2 bits baixos parecia bastar, mas um slot de textura ZERADO tem o padrao
  // 00A80000 00000003 00000000 nos dwords 3-5, e aquele 00000003 se faz passar
  // por "tipo 3 = vertice". O resultado foram 3.150 falsos fetches de vertice
  // apontando para o endereco zero, todos com tamanho zero -- o unico motivo de
  // o erro ter sido pego foi eu ter mandado marcar enderecos fora do guest.
  //
  // Quem decide de fato e o SHADER: as instrucoes vfetch/tfetch nomeiam os slots
  // que usam. E' por isso que o skate3recomp precisou da analise de shader, e
  // por que o tradutor do SDK expoe "descoberta automatica de bindings" apos a
  // traducao (include/rex/graphics/pipeline/shader/dxbc.h). O caminho e:
  // microcodigo -> tradutor -> lista de slots usados -> so entao decodificar.
  //
  // Ate la, o despejo guarda o cru. Um slot inteiramente zerado e' omitido: e'
  // ruido e esconde os que importam.
  for (uint32_t s = 0; s < 32; ++s) {
    uint32_t d[6];
    bool vazio = true;
    for (uint32_t i = 0; i < 6; ++i) {
      d[i] = LerU32(base, fetch + s * 24 + i * 4);
      if (d[i]) vazio = false;
    }
    if (vazio) continue;

    // Leitura COMO SE fosse textura, que e' a hipotese mais provavel para um
    // slot ocupado inteiro. Anotada como hipotese, nao como fato.
    const uint32_t talvez_textura = (d[0] >> 12) << 12;
    std::fprintf(g_arquivo, "  slot[%2u] %08X %08X %08X %08X %08X %08X", s, d[0], d[1], d[2],
                 d[3], d[4], d[5]);
    if ((d[0] & 3u) == 2u && Plausivel(talvez_textura)) {
      std::fprintf(g_arquivo, "   textura? base=0x%08X", talvez_textura);
    }
    std::fprintf(g_arquivo, "\n");
  }
  std::fprintf(g_arquivo, "\n");

  // Janela ao redor de device+0x329C, atras dos ponteiros de microcodigo.
  // Enderecos plausiveis sao anotados: e' o que separa um ponteiro de um campo
  // numerico qualquer. Se algum se repetir entre desenhos do mesmo objeto e
  // mudar quando o material muda, e' candidato forte.
  std::fprintf(g_arquivo, "  janela 0x%04X:", kJanelaShader);
  for (uint32_t i = 0; i < kJanelaShaderDwords; ++i) {
    std::fprintf(g_arquivo, " %08X", LerU32(base, device + kJanelaShader + i * 4));
  }
  std::fprintf(g_arquivo, "\n");
  // Os dois candidatos firmes achados no despejo anterior.
  AbrirObjetoDeShader(base, "shaderA", LerU32(base, device + 0x328C));
  AbrirObjetoDeShader(base, "shaderB", LerU32(base, device + 0x3290));

  for (uint32_t i = 0; i < kJanelaShaderDwords; ++i) {
    const uint32_t w = LerU32(base, device + kJanelaShader + i * 4);
    if (!Plausivel(w)) continue;
    // Microcodigo apontado nao comeca com duas palavras zeradas; ler duas basta
    // para descartar um campo numerico que so parecia endereco.
    const uint32_t p0 = LerU32(base, w);
    const uint32_t p1 = LerU32(base, w + 4);
    std::fprintf(g_arquivo, "    +0x%04X = 0x%08X -> [%08X %08X]%s\n",
                 kJanelaShader + i * 4, w, p0, p1, (p0 || p1) ? "  candidato" : "");
  }

  // Descarrega a cada desenho. O stdio so gravaria no fecho, e este jogo CAI em
  // cena -- o primeiro despejo real se perdeu inteiro por isso, com o arquivo
  // em zero bytes. Custa uma escrita por desenho e vale a pena: o dado que
  // interessa costuma ser justamente o do quadro que derrubou o jogo.
  std::fflush(g_arquivo);

  if (!g_restantes) {
    std::fprintf(g_arquivo, "# fim: %d desenhos despejados\n", pedidos);
    Fechar();
    // A partir daqui o gancho sai na primeira linha, sem trava.
    g_terminado.store(true, std::memory_order_relaxed);
    REXLOG_INFO("ufc3 cena: {} desenhos despejados; despejo encerrado", pedidos);
  }
}

}  // namespace extrai_cena
}  // namespace ufc3
