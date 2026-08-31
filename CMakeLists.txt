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
else()
    add_executable(ufc3 ${UFC3_SOURCES})
endif()

rexglue_setup_target(ufc3 GPU_PLUGINS xenos)
