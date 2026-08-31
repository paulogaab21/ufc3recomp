# ufc3 - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(ufc3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(generated/rexglue.cmake)

# Recursos do Windows (icone da janela e da barra de tarefas). enable_language
# em vez de mexer no project(), para nao invalidar os objetos ja compilados.
if(WIN32)
    enable_language(RC)
endif()

# Sources
set(UFC3_SOURCES
    src/main.cpp
)

if(WIN32)
    list(APPEND UFC3_SOURCES src/ufc3.rc)
endif()

if(WIN32)
    add_executable(ufc3 WIN32 ${UFC3_SOURCES})
else()
    add_executable(ufc3 ${UFC3_SOURCES})
endif()

rexglue_setup_target(ufc3 GPU_PLUGINS xenos)
