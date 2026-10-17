cmake_minimum_required(VERSION 3.20)
project(BionicleHeroesHD LANGUAGES C CXX)

if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
  message(FATAL_ERROR "Bionicle Heroes is a 32-bit game: configure with -A Win32 (or from a vcvars32 shell).")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_library(minhook STATIC
  third_party/minhook/src/buffer.c
  third_party/minhook/src/hook.c
  third_party/minhook/src/trampoline.c
  third_party/minhook/src/hde/hde32.c
)
target_include_directories(minhook PUBLIC third_party/minhook/include)

add_library(BionicleHeroesHD SHARED
  src/dllmain.cpp
  src/hd_textures.cpp
  src/hooks.cpp
  src/log.cpp
)
target_link_libraries(BionicleHeroesHD PRIVATE minhook)
target_compile_definitions(BionicleHeroesHD PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX _CRT_SECURE_NO_WARNINGS)
target_compile_options(BionicleHeroesHD PRIVATE /W4 /permissive-)
set_target_properties(BionicleHeroesHD PROPERTIES SUFFIX ".asi")
