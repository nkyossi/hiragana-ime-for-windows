cmake_minimum_required(VERSION 3.20)
project(mecab CXX)

# ---------------------------------------------------------------------------
#  MeCab 0.996 built as a static library with MSVC (x64 / x86).
#
#  Source tree: https://github.com/taku910/mecab  (mecab/src, version 0.996)
#  Two local modifications vs. upstream:
#    1. src/config.h            - written by hand (see file header)
#    2. src/mecab.h             - honours -DMECAB_STATIC (no __declspec)
#    3. src/libmecab.cpp        - lazy TLS init (an .exe never calls DllMain)
#  Everything else is stock MeCab 0.996.
# ---------------------------------------------------------------------------

if(NOT MSVC)
  message(FATAL_ERROR "This build file targets MSVC only.")
endif()

option(MECAB_BUILD_TOOLS   "Build the mecab.exe command line tool" ON)
option(MECAB_STATIC_RUNTIME "Link against the static CRT (/MT)" OFF)

set(MECAB_DIC_VERSION 102)          # = DIC_VERSION in configure.in (0.996)
set(MECAB_VERSION     "0.996")

set(MECAB_SOURCES
  char_property.cpp
  connector.cpp
  context_id.cpp
  dictionary.cpp
  dictionary_compiler.cpp
  dictionary_generator.cpp
  dictionary_rewriter.cpp
  eval.cpp
  feature_index.cpp
  iconv_utils.cpp
  lbfgs.cpp
  learner.cpp
  learner_tagger.cpp
  libmecab.cpp
  nbest_generator.cpp
  param.cpp
  string_buffer.cpp
  tagger.cpp
  tokenizer.cpp
  utils.cpp
  viterbi.cpp
  writer.cpp
)

add_library(mecab STATIC ${MECAB_SOURCES})

target_include_directories(mecab PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include/mecab>
)

target_compile_definitions(mecab PUBLIC
  MECAB_STATIC                    # <- consumers must see this too (PUBLIC)
)
target_compile_definitions(mecab PRIVATE
  HAVE_CONFIG_H
  DIC_VERSION=${MECAB_DIC_VERSION}
  MECAB_DIC_VERSION=${MECAB_DIC_VERSION}
  MECAB_USE_UTF8_ONLY             # default charset = UTF-8
  _CRT_SECURE_NO_WARNINGS
  _CRT_NONSTDC_NO_WARNINGS
  _SCL_SECURE_NO_WARNINGS
  MECAB_DEFAULT_RC="mecabrc"
)

if(MSVC)
  # MeCab 0.996 uses the C++14 `register` storage class and old idioms.
  target_compile_options(mecab PRIVATE /std:c++14 /EHsc /wd4800 /wd4305 /wd4244 /wd4267 /bigobj)
  # utils.cpp calls RegOpenKeyExW/RegQueryValueExW/RegCloseKey to look up the
  # mecabrc registry key -> advapi32 must be linked into the final image.
  target_link_libraries(mecab PUBLIC advapi32)
endif()

# ---- CRT selection -------------------------------------------------------
# The C API of MeCab is pure C, so a CRT mismatch with the consumer only
# produces link warnings, but keeping them identical avoids surprises.
if(MECAB_STATIC_RUNTIME)
  set(MECAB_MSVC_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
  set(MECAB_MSVC_RUNTIME "MultiThreaded$<$<CONFIG:Debug>:Debug>DLL")
endif()
set_property(TARGET mecab PROPERTY MSVC_RUNTIME_LIBRARY "${MECAB_MSVC_RUNTIME}")

# ---- command line tool ---------------------------------------------------
if(MECAB_BUILD_TOOLS)
  add_executable(mecab_cli mecab.cpp)
  set_target_properties(mecab_cli PROPERTIES OUTPUT_NAME mecab)
  set_property(TARGET mecab_cli PROPERTY MSVC_RUNTIME_LIBRARY "${MECAB_MSVC_RUNTIME}")
  target_link_libraries(mecab_cli PRIVATE mecab)
  target_compile_options(mecab_cli PRIVATE /std:c++14 /EHsc /wd4800 /wd4305 /wd4244)
  target_compile_definitions(mecab_cli PRIVATE MECAB_STATIC)
endif()

# ---- install layout ------------------------------------------------------
include(GNUInstallDirs)

install(TARGETS mecab ARCHIVE DESTINATION lib)
if(MECAB_BUILD_TOOLS)
  install(TARGETS mecab_cli RUNTIME DESTINATION bin)
endif()
install(FILES mecab.h config.h DESTINATION include/mecab)
