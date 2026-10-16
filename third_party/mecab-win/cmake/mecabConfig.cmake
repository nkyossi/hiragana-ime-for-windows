# mecabConfig.cmake -- find_package(mecab) support for the prebuilt
# Windows/MSVC static MeCab shipped in this directory.
#
#   find_package(mecab CONFIG REQUIRED)
#   target_link_libraries(your_target PRIVATE mecab::mecab)
#
# Exported target:
#   mecab::mecab           -- /MD flavour (dynamic CRT), matches a normal
#                             CMake+Visual Studio project default.
#   mecab::mecab-staticmt  -- /MT flavour (static CRT), self-contained exe.
#
# Both targets propagate:  include dir, -DMECAB_STATIC, and advapi32.
#
# Override the root with -Dmecab_ROOT=<path> or set mecab_DIR to this folder.

cmake_minimum_required(VERSION 3.15)

get_filename_component(_mecab_prefix "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(_mecab_arch x64)
else()
  set(_mecab_arch x86)
endif()

if(NOT TARGET mecab::mecab)
  add_library(mecab::mecab STATIC IMPORTED)
  set_target_properties(mecab::mecab PROPERTIES
    IMPORTED_LOCATION              "${_mecab_prefix}/lib/${_mecab_arch}/mecab.lib"
    INTERFACE_INCLUDE_DIRECTORIES  "${_mecab_prefix}/include"
    INTERFACE_COMPILE_DEFINITIONS  "MECAB_STATIC"
    INTERFACE_LINK_LIBRARIES       "advapi32")
endif()

if(NOT TARGET mecab::mecab-staticmt)
  add_library(mecab::mecab-staticmt STATIC IMPORTED)
  set_target_properties(mecab::mecab-staticmt PROPERTIES
    IMPORTED_LOCATION              "${_mecab_prefix}/lib-mt/${_mecab_arch}/mecab.lib"
    INTERFACE_INCLUDE_DIRECTORIES  "${_mecab_prefix}/include"
    INTERFACE_COMPILE_DEFINITIONS  "MECAB_STATIC"
    INTERFACE_LINK_LIBRARIES       "advapi32")
endif()

set(mecab_FOUND TRUE)
set(mecab_VERSION "0.996")
set(mecab_DIC_VERSION 102)
set(mecab_ARCH "${_mecab_arch}")
set(MECAB_DICDIR "${_mecab_prefix}/dicdir" CACHE PATH "unidic-lite dicdir to ship")
