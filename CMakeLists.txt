# ufc3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ufc3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

set(UFC3_SOURCES
    src/main.cpp
    src/ufc3_native_render.cpp
    src/ufc3_draw_capture.cpp
    src/ufc3_draw_state.cpp
    src/ufc3_scene_extract.cpp
    src/ufc3_shader_fetch.cpp
    src/ufc3_frame_mark.cpp
    src/ufc3_shader_translate.cpp
)

if(WIN32)
    # Recursos do Windows: o icone que aparece na janela, na barra de tarefas e
    # no Alt+Tab, mais a identificacao de versao do executavel. O SDK nao define
    # icone em tempo de execucao, entao este e o unico lugar que decide isso.
    #
    # enable_language em vez de mexer no project(), para nao invalidar os
    # objetos ja compilados a cada reconfiguracao.
    enable_language(RC)
    list(APPEND UFC3_SOURCES src/ufc3.rc)

    # WIN32 no add_executable: subsistema grafico, sem console atras da janela.
    add_executable(ufc3 WIN32 ${UFC3_SOURCES})
elseif(ANDROID)
    # No Android nao existe processo do jogo: quem roda e a Activity do SDL,
    # que carrega uma biblioteca por System.loadLibrary e chama SDL_main dentro
    # dela. O alvo precisa sair como libufc3.so, nao como executavel.
    add_library(ufc3 SHARED ${UFC3_SOURCES})
    set_target_properties(ufc3 PROPERTIES PREFIX "lib" OUTPUT_NAME "ufc3")
else()
    add_executable(ufc3 ${UFC3_SOURCES})
endif()

# Graficos compilados DENTRO do executavel, no modelo do skate3recomp.
#
# O padrao do rexglue 0.10.0 e carregar a GPU como plugin por dlopen. Isso
# funciona para jogar, mas fecha a porta do renderizador nativo: o tradutor de
# shader do Xenos, o cache de textura e a RHI vivem todos dentro do plugin, e o
# runtime que este executavel linka nao carrega nada de graficos. Qualquer
# tentativa de chamar o tradutor daqui falha no link, com os simbolos
# indefinidos -- nao importa onde a chamada esteja.
#
# O skate3recomp nao teve esse problema porque no 0.8.0 os graficos ja iam
# compilados no executavel. REXGLUE_GPU_STATIC devolve esse arranjo.
set(REXGLUE_GPU_STATIC ON CACHE BOOL "" FORCE)

rexglue_setup_target(ufc3 GPU_PLUGINS xenos)

# Com a GPU estatica o executavel linka o alvo direto, e o sistema de graficos
# passa a ser fornecido pelo proprio app (ver OnPreSetup em ufc3_app.h) em vez
# de procurado como plugin.
target_link_libraries(ufc3 PRIVATE rex::gpu-xenos)

if(WIN32)
    # MiniDumpWriteDump, usado por src/crash_capture.h para gravar o despejo
    # quando o jogo morre.
    target_link_libraries(ufc3 PRIVATE Dbghelp)
endif()
