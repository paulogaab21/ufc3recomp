// ufc3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "captura_falhas.h"
#include "ufc3_render_nativo.h"

class Ufc3App : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    return std::unique_ptr<Ufc3App>(new Ufc3App(ctx, "ufc3",
        PPCImageConfig));
  }

  // O ReXGlue nao instala manipulador de excecao, entao um crash duro nao
  // deixava nada para investigar: o processo sumia e o log parava no meio de
  // uma linha. Instalado aqui porque o log ja esta de pe e o jogo ainda nao
  // comecou a rodar.
  void OnPostInitLogging() override { ufc3::captura::Instalar(); }

  // O renderizador nativo se registra antes do primeiro quadro. Registrar nao
  // muda nada por si: enquanto ele ceder todos os quadros, o jogo desenha por
  // emulacao exatamente como antes.
  void OnPostSetup() override { ufc3::render_nativo::Registrar(); }

  // O SDK nao expoe um gancho "uma vez por quadro", mas todo dialogo de ImGui
  // recebe OnDraw a cada quadro na thread da interface. Este nao desenha nada:
  // existe so para dar o passo de tempo ao desvanecimento do fundo do menu e
  // para observar quando o menu abre e fecha.
  class RelogioDoMenu final : public rex::ui::ImGuiDialog {
   public:
    RelogioDoMenu(rex::ui::ImGuiDrawer* desenhista, const Ufc3App* app)
        : rex::ui::ImGuiDialog(desenhista), app_(app) {}

   protected:
    void OnDraw(ImGuiIO& io) override {
      ufc3::render_nativo::AoAbrirFecharMenu(app_->IsSettingsOverlayOpen());
      ufc3::render_nativo::AvancarQuadro(io.DeltaTime);
    }

   private:
    const Ufc3App* app_;
  };

  void OnCreateDialogs(rex::ui::ImGuiDrawer* desenhista) override {
    // O dialogo se registra no desenhista ao nascer e se apaga sozinho ao
    // fechar, entao nao ha nada a guardar aqui.
    new RelogioDoMenu(desenhista, this);
  }

  // Override virtual hooks for customization:
  // void OnPreSetup(rex::RuntimeConfig& config) override {}
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // void OnPostSetup() override {}
  // void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}
  // void OnConfigurePaths(rex::PathConfig& paths) override {}
};
