// ufc3 - native renderer
//
// The normal path draws through emulation: the game writes PM4 packets into the
// command ring, the command processor interprets them and rebuilds every frame
// by translating Xenos state. It works, but it pays the cost of emulating a
// whole GPU to reach an image a modern card could have drawn directly.
//
// The SDK exposes two entry points, and the difference between them decides what
// is achievable today:
//
//   renderer        Called BEFORE the emulated path builds the image. Taking the
//                   frame here means drawing the whole scene by reading the
//                   game's structures out of guest memory -- and we cannot read
//                   all of them yet. Until we can, it yields.
//
//   post-processor  Called AFTER, with the finished frame in hand. Here native
//                   drawing does real work on EVERY frame, and the result shows
//                   up on screen.
//
// It is through the second that the native renderer already earns its place. It
// does two things:
//
//   sharpening  The game draws at 1280x720 and the image is scaled up to the
//               display. Scaling like that softens everything. Contrast-adaptive
//               sharpening recovers definition where there is detail and leaves
//               flat areas alone, which is where a plain sharpen creates halos.
//
//   blur        Backdrop for the settings menu, so the text stays readable over
//               any scene.
//
// Both go through the same skeleton: a full-screen triangle, two passes and an
// intermediate target -- reading and writing the same texture in one pass is not
// possible.
//
// There is no switch to turn the native path off. It is not an optional effect
// layered on the game: it is where the final image goes through. What the player
// chooses is the STRENGTH (ufc3_nitidez, zero included), not whether the
// renderer takes part -- the same way nobody chooses whether emulation draws.

#include "ufc3_native_render.h"

#include <algorithm>
#include <cstdint>

#include <rex/cvar.h>
#include <rex/graphics/native_guest_renderer.h>
#include <rex/graphics/native_rhi.h>
#include <rex/logging.h>

REXCVAR_DEFINE_INT32(ufc3_nitidez, 55, "Video/Native renderer",
                     "Contrast-adaptive sharpening over the final image, as a percentage. "
                     "The game draws at 720p and scaling softens it; this recovers the "
                     "definition. 0 turns it off.")
    .range(0, 100);

REXCVAR_DEFINE_BOOL(ufc3_render_nativo_teste, false, "Diagnostics",
                    "Paints the output a solid colour through the native path. Only to "
                    "verify that the hook, the RHI and presentation are all standing.");

REXCVAR_DEFINE_BOOL(ufc3_menu_desfoque, true, "Video/Interface",
                    "Blurs and darkens the game behind the settings menu, so the text "
                    "stays readable over any scene.");

REXCVAR_DEFINE_INT32(ufc3_menu_desfoque_forca, 100, "Video/Interface",
                     "Strength of the menu blur, as a percentage.")
    .range(0, 300);

namespace ufc3 {
namespace render_nativo {

namespace {

namespace nrhi = rex::graphics::nrhi;
using rex::graphics::NativeGuestOutputRenderContext;
using nrhi::ResourceState;

// Um triangulo que cobre a tela inteira, gerado do indice do vertice -- sem
// buffer de vertices, sem buffer de indices. Dois triangulos costurados teriam
// uma diagonal onde as amostras se repetem; um triangulo maior que a tela nao
// tem costura nenhuma.
constexpr const char* kShaders = R"(
Texture2D<float4> fonte : register(t0);
SamplerState amostrador : register(s0);

cbuffer Constantes : register(b0) {
  float2 passo;      // desfoque: direcao / tamanho.  nitidez: 1 / tamanho.
  float  escurecer;  // 1 = nao escurece
  float  forca;      // nitidez: 0..1
};

struct Saida {
  float4 pos : SV_Position;
  float2 uv  : TEXCOORD0;
};

Saida vs_principal(uint id : SV_VertexID) {
  Saida o;
  o.uv = float2((id << 1) & 2, id & 2);
  o.pos = float4(o.uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
  return o;
}

// Desfoque gaussiano separavel: uma passada horizontal e outra vertical dao o
// mesmo resultado de um nucleo 2D por uma fracao das amostras.
float4 ps_desfoque(Saida e) : SV_Target {
  const float pesos[5] = { 0.227027, 0.194594, 0.121621, 0.054054, 0.016216 };
  float4 cor = fonte.SampleLevel(amostrador, e.uv, 0) * pesos[0];
  [unroll]
  for (int i = 1; i < 5; ++i) {
    float2 d = passo * float(i);
    cor += fonte.SampleLevel(amostrador, e.uv + d, 0) * pesos[i];
    cor += fonte.SampleLevel(amostrador, e.uv - d, 0) * pesos[i];
  }
  return float4(cor.rgb * escurecer, 1.0);
}

// Nitidez adaptativa ao contraste.
//
// Um "sharpen" comum soma a mesma quantidade em todo pixel, e por isso cria
// auréola clara nas bordas de alto contraste e realca ruido nas areas lisas.
// Aqui a quantidade sai do proprio vizinhado: onde ja ha muita variacao, quase
// nada e' somado; onde ha detalhe fino com folga para crescer, soma-se mais.
// O denominador (1 + 4w) renormaliza, entao o brilho medio nao muda.
//
// Com forca = 0 o peso zera e a funcao vira uma copia exata -- e' assim que a
// segunda passada devolve a imagem sem tocar nela.
float4 ps_nitidez(Saida e) : SV_Target {
  float3 c = fonte.SampleLevel(amostrador, e.uv, 0).rgb;
  if (forca <= 0.0) {
    return float4(c, 1.0);
  }

  float3 n = fonte.SampleLevel(amostrador, e.uv + float2(0.0, -passo.y), 0).rgb;
  float3 s = fonte.SampleLevel(amostrador, e.uv + float2(0.0,  passo.y), 0).rgb;
  float3 w = fonte.SampleLevel(amostrador, e.uv + float2(-passo.x, 0.0), 0).rgb;
  float3 t = fonte.SampleLevel(amostrador, e.uv + float2( passo.x, 0.0), 0).rgb;

  float3 menor = min(c, min(min(n, s), min(w, t)));
  float3 maior = max(c, max(max(n, s), max(w, t)));

  // Quanto ainda cabe crescer sem estourar, por canal.
  float3 folga = saturate(min(menor, 1.0 - maior) / max(maior, 1e-5));
  float  peso  = -sqrt(max(folga.g, 0.0)) * forca * 0.2;

  float3 saida = (c + (n + s + w + t) * peso) / (1.0 + 4.0 * peso);
  return float4(saturate(saida), 1.0);
}
)";

struct Constantes {
  float passo[2];
  float escurecer;
  float forca;
};

// Recursos criados sob demanda, no primeiro quadro que precisa deles. Com a
// nitidez em zero e o menu fechado nao ha o que desenhar, e nada disto chega a
// existir.
struct Recursos {
  nrhi::Device* dispositivo = nullptr;
  nrhi::BindingLayout* layout = nullptr;
  nrhi::Shader* vs = nullptr;
  nrhi::Shader* ps_desfoque = nullptr;
  nrhi::Shader* ps_nitidez = nullptr;
  nrhi::Pipeline* pipe_desfoque = nullptr;
  nrhi::Pipeline* pipe_nitidez = nullptr;

  // Alvo intermediario entre as duas passadas.
  nrhi::Texture* meio = nullptr;
  nrhi::TextureView* meio_srv = nullptr;
  uint32_t largura = 0;
  uint32_t altura = 0;

  // O apresentador NAO entrega sempre a mesma imagem de saida: ele mantem um
  // rodizio de algumas, e a cada quadro pode vir outra. Guardar uma vista so,
  // como esta struct fazia, significava destruir e recriar a vista em TODO
  // quadro -- descritor novo, destruicao diferida, e a taxa de quadros caindo de
  // 60 para 30 ao longo da partida.
  //
  // Um punhado de entradas cobre o rodizio inteiro com folga.
  static constexpr uint32_t kMaxSaidas = 8;
  nrhi::Texture* saida_tex[kMaxSaidas] = {};
  nrhi::TextureView* saida_srv[kMaxSaidas] = {};
  uint32_t saidas_usadas = 0;
};

Recursos g_r;

// Sobe enquanto o menu esta aberto e desce quando fecha, para o fundo nao
// aparecer e sumir num estalo.
float g_intensidade = 0.0f;
bool g_menu_aberto = false;

bool GarantirPipelines(nrhi::Device* dispositivo) {
  if (g_r.pipe_nitidez && g_r.dispositivo == dispositivo) {
    return true;
  }
  g_r.dispositivo = dispositivo;

  nrhi::BindingLayoutDesc bl;
  bl.params[0].kind = nrhi::BindingParamKind::kConstants;
  bl.params[0].shader_register = 0;
  bl.params[0].count = sizeof(Constantes) / sizeof(uint32_t);
  bl.params[0].visibility = nrhi::Visibility::kPixel;
  bl.params[1].kind = nrhi::BindingParamKind::kTextureTable;
  bl.params[1].shader_register = 0;
  bl.params[1].count = 1;
  bl.params[1].visibility = nrhi::Visibility::kPixel;
  bl.param_count = 2;
  bl.static_samplers[0].shader_register = 0;
  bl.static_samplers[0].filter = nrhi::Filter::kLinear;
  bl.static_samplers[0].address = nrhi::AddressMode::kClamp;
  bl.static_sampler_count = 1;
  // Sem buffer de vertices: o triangulo sai do SV_VertexID.
  bl.allow_input_layout = false;
  g_r.layout = dispositivo->CreateBindingLayout(bl);
  if (!g_r.layout) {
    return false;
  }

  nrhi::ShaderDesc vs{};
  vs.stage = nrhi::ShaderStage::kVertex;
  vs.name = "ufc3_tela_cheia_vs";
  vs.hlsl_source = kShaders;
  vs.entry_point = "vs_principal";
  g_r.vs = dispositivo->CreateShader(vs);

  nrhi::ShaderDesc pd_{};
  pd_.stage = nrhi::ShaderStage::kPixel;
  pd_.name = "ufc3_desfoque_ps";
  pd_.hlsl_source = kShaders;
  pd_.entry_point = "ps_desfoque";
  g_r.ps_desfoque = dispositivo->CreateShader(pd_);

  nrhi::ShaderDesc pn{};
  pn.stage = nrhi::ShaderStage::kPixel;
  pn.name = "ufc3_nitidez_ps";
  pn.hlsl_source = kShaders;
  pn.entry_point = "ps_nitidez";
  g_r.ps_nitidez = dispositivo->CreateShader(pn);

  if (!g_r.vs || !g_r.ps_desfoque || !g_r.ps_nitidez) {
    REXLOG_ERROR("ufc3: falhou compilar os shaders do renderizador nativo");
    return false;
  }

  nrhi::GraphicsPipelineDesc p{};
  p.layout = g_r.layout;
  p.vs = g_r.vs;
  p.cull = nrhi::CullMode::kNone;
  p.depth_clip = false;
  p.rtv_format = nrhi::Format::kR10G10B10A2_UNORM;

  p.ps = g_r.ps_desfoque;
  g_r.pipe_desfoque = dispositivo->CreateGraphicsPipeline(p);
  p.ps = g_r.ps_nitidez;
  g_r.pipe_nitidez = dispositivo->CreateGraphicsPipeline(p);

  const bool ok = g_r.pipe_desfoque && g_r.pipe_nitidez;
  if (ok) {
    REXLOG_INFO("ufc3: pipelines do renderizador nativo prontas");
  }
  return ok;
}

bool GarantirAlvos(nrhi::Device* dispositivo, nrhi::Texture* saida, uint32_t largura,
                   uint32_t altura) {
  if (!g_r.meio || g_r.largura != largura || g_r.altura != altura) {
    if (g_r.meio_srv) dispositivo->DestroyDeferred(g_r.meio_srv);
    if (g_r.meio) dispositivo->DestroyDeferred(g_r.meio);
    g_r.meio_srv = nullptr;
    g_r.meio = nullptr;

    nrhi::TextureDesc td{};
    td.width = largura;
    td.height = altura;
    td.format = saida->format();
    td.usage = nrhi::kTextureUsageRenderTarget;
    // Nasce ja no estado em que a primeira passada espera encontra-lo, para a
    // barreira do primeiro quadro nao mentir sobre o estado anterior.
    td.initial_state = ResourceState::kPixelShaderResource;
    g_r.meio = dispositivo->CreateTexture(td);
    if (!g_r.meio) {
      return false;
    }
    g_r.meio_srv = dispositivo->CreateTextureView(g_r.meio, nrhi::TextureViewDesc{});
    g_r.largura = largura;
    g_r.altura = altura;
  }

  return g_r.meio_srv != nullptr;
}

// Vista da imagem de saida, criada uma vez por imagem do rodizio do
// apresentador. Devolve nullptr so se a criacao falhar.
nrhi::TextureView* VistaDaSaida(nrhi::Device* dispositivo, nrhi::Texture* saida) {
  for (uint32_t i = 0; i < g_r.saidas_usadas; ++i) {
    if (g_r.saida_tex[i] == saida) {
      return g_r.saida_srv[i];
    }
  }
  if (g_r.saidas_usadas >= Recursos::kMaxSaidas) {
    // The presenter's rotation should never exceed this. If it does, start the
    // cache over -- but release EVERY entry first.
    //
    // The first version of this released only entry 0 and then reset the count,
    // which orphaned the other seven: their views were never destroyed and their
    // slots were overwritten. Seven descriptors leaked per overflow, quietly,
    // and the only symptom would be the frame time creeping up over a long
    // session.
    for (uint32_t i = 0; i < g_r.saidas_usadas; ++i) {
      if (g_r.saida_srv[i]) {
        dispositivo->DestroyDeferred(g_r.saida_srv[i]);
      }
      g_r.saida_srv[i] = nullptr;
      g_r.saida_tex[i] = nullptr;
    }
    g_r.saidas_usadas = 0;
  }
  auto* vista = dispositivo->CreateTextureView(saida, nrhi::TextureViewDesc{});
  if (!vista) {
    return nullptr;
  }
  g_r.saida_tex[g_r.saidas_usadas] = saida;
  g_r.saida_srv[g_r.saidas_usadas] = vista;
  ++g_r.saidas_usadas;
  return vista;
}

void Passada(nrhi::Cmd* cmd, nrhi::Pipeline* pipeline, nrhi::TextureView* fonte,
             const Constantes& c, uint32_t largura, uint32_t altura) {
  nrhi::Viewport vp;
  vp.width = float(largura);
  vp.height = float(altura);
  cmd->SetViewport(vp);

  nrhi::Rect tesoura;
  tesoura.right = int32_t(largura);
  tesoura.bottom = int32_t(altura);
  cmd->SetScissor(tesoura);

  cmd->SetBindingLayout(g_r.layout);
  cmd->SetPipeline(pipeline);
  cmd->SetRootConstants(0, sizeof(Constantes) / sizeof(uint32_t), &c, 0);
  cmd->SetTexture(1, fonte);
  cmd->SetPrimitiveTopology(nrhi::PrimitiveTopology::kTriangleList);
  cmd->Draw(3, 0);
}

// ---------------------------------------------------------------------------
//  Pos-processamento: onde o renderizador nativo trabalha hoje
// ---------------------------------------------------------------------------
void PosProcessar(const NativeGuestOutputRenderContext& contexto, void* /*dados*/) {
  auto* cmd = contexto.cmd;
  auto* saida = contexto.guest_output;
  auto* dispositivo = contexto.device;
  if (!cmd || !saida || !dispositivo) {
    return;
  }

  const float intensidade = g_intensidade;
  const bool quer_desfoque = intensidade > 0.0f;
  const int pct_nitidez = REXCVAR_GET(ufc3_nitidez);
  const bool quer_nitidez = pct_nitidez > 0;

  if (!quer_desfoque && !quer_nitidez) {
    // Nada a fazer: baixa a bandeira para os proximos quadros nem montarem o
    // quadro da RHI.
    rex::graphics::RequestNativeGuestOutputPostProcess(false);
    return;
  }

  const uint32_t largura = contexto.guest_output_width;
  const uint32_t altura = contexto.guest_output_height;
  if (!largura || !altura) {
    return;
  }
  if (!GarantirPipelines(dispositivo) || !GarantirAlvos(dispositivo, saida, largura, altura)) {
    return;
  }
  nrhi::TextureView* const saida_srv = VistaDaSaida(dispositivo, saida);
  if (!saida_srv) {
    return;
  }

  Constantes primeira{};
  Constantes segunda{};
  nrhi::Pipeline* pipe_primeira = nullptr;
  nrhi::Pipeline* pipe_segunda = nullptr;

  if (quer_desfoque) {
    // O menu manda: desfoque separavel, horizontal e depois vertical. Escurecer
    // so na segunda passada, senao o fator entraria ao quadrado.
    const float forca =
        intensidade * float(std::clamp(REXCVAR_GET(ufc3_menu_desfoque_forca), 0, 300)) / 100.0f;
    pipe_primeira = g_r.pipe_desfoque;
    pipe_segunda = g_r.pipe_desfoque;
    primeira.passo[0] = forca / float(largura);
    primeira.escurecer = 1.0f;
    segunda.passo[1] = forca / float(altura);
    segunda.escurecer = 1.0f - 0.45f * intensidade;
  } else {
    // Nitidez na ida; a volta e' copia pura (forca zero).
    pipe_primeira = g_r.pipe_nitidez;
    pipe_segunda = g_r.pipe_nitidez;
    primeira.passo[0] = 1.0f / float(largura);
    primeira.passo[1] = 1.0f / float(altura);
    primeira.escurecer = 1.0f;
    primeira.forca = float(pct_nitidez) / 100.0f;
    segunda.escurecer = 1.0f;
    segunda.forca = 0.0f;
  }

  // Ida: a imagem do jogo alimenta o alvo intermediario.
  cmd->Barrier(saida, ResourceState::kGuestOutput, ResourceState::kPixelShaderResource);
  cmd->Barrier(g_r.meio, ResourceState::kPixelShaderResource, ResourceState::kRenderTarget);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(g_r.meio, nullptr);
  Passada(cmd, pipe_primeira, saida_srv, primeira, largura, altura);

  // Volta: o intermediario devolve a imagem de saida.
  cmd->Barrier(g_r.meio, ResourceState::kRenderTarget, ResourceState::kPixelShaderResource);
  cmd->Barrier(saida, ResourceState::kPixelShaderResource, ResourceState::kRenderTarget);
  cmd->FlushBarriers();
  cmd->SetRenderTargets(saida, nullptr);
  Passada(cmd, pipe_segunda, g_r.meio_srv, segunda, largura, altura);

  // A imagem precisa voltar ao estado do apresentador antes de sairmos daqui.
  cmd->Barrier(saida, ResourceState::kRenderTarget, ResourceState::kGuestOutput);
  cmd->FlushBarriers();
}

// ---------------------------------------------------------------------------
//  Renderizador: assumir o quadro inteiro ainda depende de ler a cena do guest
// ---------------------------------------------------------------------------
bool DesenharQuadro(const NativeGuestOutputRenderContext& contexto, void* /*dados*/) {
  if (!REXCVAR_GET(ufc3_render_nativo_teste)) {
    return false;
  }

  auto* cmd = contexto.cmd;
  auto* saida = contexto.guest_output;
  if (!cmd || !saida) {
    return false;
  }

  cmd->Barrier(saida, ResourceState::kGuestOutput, ResourceState::kRenderTarget);
  cmd->FlushBarriers();

  static uint32_t quadro = 0;
  // Um leve pulsar deixa obvio que sao quadros novos, e nao uma imagem
  // congelada de um quadro que travou.
  const float pulso = 0.25f + 0.15f * float((quadro++ >> 3) & 1);
  const float cor[4] = {0.0f, pulso, 0.55f, 1.0f};
  cmd->ClearRenderTarget(saida, cor);

  cmd->Barrier(saida, ResourceState::kRenderTarget, ResourceState::kGuestOutput);
  cmd->FlushBarriers();
  return true;
}

}  // namespace

void Registrar() {
  rex::graphics::SetNativeGuestOutputRenderer(&DesenharQuadro, nullptr);
  rex::graphics::SetNativeGuestOutputPostProcessor(&PosProcessar, nullptr);
  // Pedido inicial: sem isto o primeiro quadro passaria pela porteira do
  // processador de comandos sem montar o quadro da RHI, e a nitidez so
  // comecaria depois que algo mais levantasse a bandeira.
  if (REXCVAR_GET(ufc3_nitidez) > 0) {
    rex::graphics::RequestNativeGuestOutputPostProcess(true);
  }
  REXLOG_INFO("ufc3: renderizador nativo ativo (nitidez={}%, desfoque={})",
              REXCVAR_GET(ufc3_nitidez), REXCVAR_GET(ufc3_menu_desfoque));
}

void AoAbrirFecharMenu(bool aberto) { g_menu_aberto = aberto; }

void AvancarQuadro(float segundos) {
  const bool quer = g_menu_aberto && REXCVAR_GET(ufc3_menu_desfoque);
  // Entra em ~0,12 s e sai em ~0,18 s: rapido o bastante para nao parecer
  // preguica, lento o bastante para nao piscar.
  const float taxa = quer ? (1.0f / 0.12f) : -(1.0f / 0.18f);
  g_intensidade = std::clamp(g_intensidade + taxa * segundos, 0.0f, 1.0f);

  // A bandeira e' uma porteira barata do lado do processador de comandos: sem
  // ela, todo quadro sem efeito pagaria a montagem do quadro da RHI a toa. Ela
  // sobe aqui e o proprio pos-processador a baixa quando nao ha mais o que
  // fazer.
  const bool ha_trabalho = g_intensidade > 0.0f || REXCVAR_GET(ufc3_nitidez) > 0;
  if (ha_trabalho) {
    rex::graphics::RequestNativeGuestOutputPostProcess(true);
  }
}

}  // namespace render_nativo
}  // namespace ufc3
