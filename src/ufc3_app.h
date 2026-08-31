// ufc3 - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/rex_app.h>

#include "captura_falhas.h"

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
