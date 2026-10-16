cmake_minimum_required(VERSION 3.20)
project(HiraganaIME VERSION 1.1.3 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(MSVC)
    add_compile_options(/utf-8 /W4 /permissive- /EHsc /wd4100)
    add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN)
endif()

set(UPSTREAM_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ibus-hiragana)

# Pure conversion logic ported from ibus-hiragana/engine.
add_library(hiragana_core STATIC
    src/core/Text.cpp
    src/core/KanaTables.cpp
    src/core/Log.cpp
    src/core/Dictionary.cpp
    src/core/Json.cpp
    src/core/Keysyms.cpp
    src/core/KeyboardController.cpp
    src/core/LookupTable.cpp
    src/core/Engine.cpp
    src/core/Settings.cpp
    src/core/Nfkc.cpp
)
target_include_directories(hiragana_core PUBLIC src/core)
# NormalizeString (NFKC) of the BERT tokenizer.
target_link_libraries(hiragana_core PUBLIC Normaliz)

# FuriganaPad: the editor part that does not depend on the UI (LGPL-2.1-or-later).
add_library(furiganapad_core STATIC
    src/furiganapad/core/Annotation.cpp
    src/furiganapad/core/Breaker.cpp
    src/furiganapad/core/TextBuffer.cpp
    src/furiganapad/core/InputContext.cpp
    src/furiganapad/core/Markdown.cpp
)
target_include_directories(furiganapad_core PUBLIC src/furiganapad/core)
# ICU of Windows 10 version 1903 or later
target_link_libraries(furiganapad_core PUBLIC icu)

# LLM-assisted conversion (ONNX Runtime + MeCab). The backend is built only
# when both dependencies are available. Neither the runtime, nor MeCab, nor the
# model is distributed with this source tree; see tools/fetch-onnxruntime.ps1
# and tools/export-llm-model.py.
set(HIRAGANA_ONNXRUNTIME_DIR "" CACHE PATH "ONNX Runtime C++ distribution (a root with include/ and lib/)")
set(HIRAGANA_MECAB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mecab-win" CACHE PATH "MeCab root (include/mecab.h and lib/<arch>/mecab.lib)")
set(HIRAGANA_MECAB_DIC_DIR "" CACHE PATH "unidic-lite dicdir (sys.dic, matrix.bin, ...)")
set(HIRAGANA_LLM_MODEL "" CACHE FILEPATH "The exported model.onnx for the LLM backend")

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
    set(HIRAGANA_MECAB_ARCH x64)
else()
    set(HIRAGANA_MECAB_ARCH x86)
endif()

set(HIRAGANA_ONNX_BACKEND OFF)
if(HIRAGANA_ONNXRUNTIME_DIR AND EXISTS "${HIRAGANA_ONNXRUNTIME_DIR}/include/onnxruntime_cxx_api.h")
    set(HIRAGANA_ONNX_BACKEND ON)
endif()
set(HIRAGANA_MECAB_BACKEND OFF)
if(HIRAGANA_MECAB_DIR AND EXISTS "${HIRAGANA_MECAB_DIR}/include/mecab.h" AND
        EXISTS "${HIRAGANA_MECAB_DIR}/lib/${HIRAGANA_MECAB_ARCH}/mecab.lib")
    set(HIRAGANA_MECAB_BACKEND ON)
endif()
set(HIRAGANA_LLM_BACKEND OFF)
if(HIRAGANA_ONNX_BACKEND AND HIRAGANA_MECAB_BACKEND)
    set(HIRAGANA_LLM_BACKEND ON)
endif()

if(HIRAGANA_ONNX_BACKEND)
    add_library(hiragana_onnx STATIC
        src/core/OnnxBert.cpp
    )
    target_include_directories(hiragana_onnx PUBLIC src/core "${HIRAGANA_ONNXRUNTIME_DIR}/include")
    target_link_libraries(hiragana_onnx PUBLIC "${HIRAGANA_ONNXRUNTIME_DIR}/lib/onnxruntime.lib")
endif()

# The BERT tokenizer (MeCab + WordPiece) and the port of llm.py.
if(HIRAGANA_LLM_BACKEND)
    add_library(hiragana_llm STATIC
        src/core/MecabTagger.cpp
        src/core/BertTokenizer.cpp
        src/core/LlamaAssist.cpp
    )
    target_include_directories(hiragana_llm PUBLIC src/core "${HIRAGANA_MECAB_DIR}/include")
    target_compile_definitions(hiragana_llm PUBLIC MECAB_STATIC)
    target_link_libraries(hiragana_llm PUBLIC
        hiragana_core
        hiragana_onnx
        "${HIRAGANA_MECAB_DIR}/lib/${HIRAGANA_MECAB_ARCH}/mecab.lib"
        advapi32)
endif()

option(HIRAGANA_BUILD_TESTS "Build the core tests" ON)
option(HIRAGANA_BUILD_TSF "Build the TSF text service and the setup program" ON)

if(HIRAGANA_BUILD_TESTS)
    enable_testing()
    add_executable(core_tests tests/core_tests.cpp tests/EngineTests.cpp tests/test_main.cpp)
    target_link_libraries(core_tests PRIVATE hiragana_core)
    target_compile_definitions(core_tests PRIVATE UPSTREAM_DIR="${UPSTREAM_DIR}")
    add_test(NAME core_tests COMMAND core_tests)

    add_executable(furiganapad_tests tests/furiganapad_tests.cpp tests/test_main.cpp)
    target_link_libraries(furiganapad_tests PRIVATE furiganapad_core hiragana_core)
    target_include_directories(furiganapad_tests PRIVATE tests)
    target_compile_definitions(furiganapad_tests PRIVATE UPSTREAM_DIR="${UPSTREAM_DIR}")
    add_test(NAME furiganapad_tests COMMAND furiganapad_tests)

    # End-to-end test of the installed text service; run it by hand (it types with SendInput).
    if(WIN32)
        add_executable(tsf_test tests/tsf_test.cpp)
        target_link_libraries(tsf_test PRIVATE ole32 user32 gdi32)
    endif()

    add_executable(golden_test tests/golden_test.cpp)
    target_link_libraries(golden_test PRIVATE hiragana_core)
    find_package(Python3 COMPONENTS Interpreter)
    if(Python3_FOUND)
        set(GOLDEN_DIR ${CMAKE_CURRENT_BINARY_DIR}/golden)
        file(MAKE_DIRECTORY ${GOLDEN_DIR})
        add_test(NAME golden_generate
            COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_golden.py ${GOLDEN_DIR})
        set_tests_properties(golden_generate PROPERTIES FIXTURES_SETUP golden)
        add_executable(engine_replay tests/engine_replay.cpp)
        target_link_libraries(engine_replay PRIVATE hiragana_core)
        add_test(NAME engine_golden_generate
            COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_engine_golden.py
                ${GOLDEN_DIR}/engine.txt 40)
        set_tests_properties(engine_golden_generate PROPERTIES FIXTURES_SETUP engine_golden)
        add_test(NAME engine_golden COMMAND engine_replay ${UPSTREAM_DIR} ${GOLDEN_DIR}/engine.txt)
        set_tests_properties(engine_golden PROPERTIES FIXTURES_REQUIRED engine_golden)

        foreach(system restrained.9 restrained.1)
            foreach(kind romazi kana)
                add_test(NAME golden_${system}_${kind}
                    COMMAND golden_test ${UPSTREAM_DIR}/dic ${system}.dic ${kind}
                        ${GOLDEN_DIR}/lookup.${system}.${kind}.tsv)
                set_tests_properties(golden_${system}_${kind} PROPERTIES FIXTURES_REQUIRED golden)
            endforeach()
        endforeach()

        add_executable(assisted_golden_test tests/assisted_golden_test.cpp)
        target_link_libraries(assisted_golden_test PRIVATE hiragana_core)
        add_test(NAME assisted_golden_generate
            COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_llm_golden.py ${GOLDEN_DIR})
        set_tests_properties(assisted_golden_generate PROPERTIES FIXTURES_SETUP assisted_golden)
        foreach(system restrained.9 restrained.1)
            foreach(kind romazi kana)
                add_test(NAME assisted_golden_${system}_${kind}
                    COMMAND assisted_golden_test ${UPSTREAM_DIR}/dic ${system}.dic ${kind}
                        ${GOLDEN_DIR}/assisted.${system}.${kind}.tsv)
                set_tests_properties(assisted_golden_${system}_${kind} PROPERTIES FIXTURES_REQUIRED assisted_golden)
            endforeach()
        endforeach()

        add_executable(nfkc_test tests/nfkc_test.cpp)
        target_link_libraries(nfkc_test PRIVATE hiragana_core)
        add_test(NAME nfkc_generate
            COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_nfkc_golden.py
                ${GOLDEN_DIR}/nfkc.txt)
        set_tests_properties(nfkc_generate PROPERTIES FIXTURES_SETUP nfkc)
        add_test(NAME nfkc COMMAND nfkc_test ${GOLDEN_DIR}/nfkc.txt)
        set_tests_properties(nfkc PROPERTIES FIXTURES_REQUIRED nfkc)

        if(HIRAGANA_ONNX_BACKEND AND HIRAGANA_LLM_MODEL AND EXISTS "${HIRAGANA_LLM_MODEL}")
            add_executable(onnx_bert_test tests/onnx_bert_test.cpp)
            target_link_libraries(onnx_bert_test PRIVATE hiragana_onnx)
            add_test(NAME onnx_bert_generate
                COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                    ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_onnx_bert_golden.py
                    ${GOLDEN_DIR}/onnx_bert.txt "${HIRAGANA_LLM_MODEL}")
            set_tests_properties(onnx_bert_generate PROPERTIES FIXTURES_SETUP onnx_bert_golden)
            add_test(NAME onnx_bert
                COMMAND onnx_bert_test "${HIRAGANA_LLM_MODEL}" ${GOLDEN_DIR}/onnx_bert.txt)
            set_tests_properties(onnx_bert PROPERTIES FIXTURES_REQUIRED onnx_bert_golden)
            # The runtime DLL must sit next to the test executable.
            add_custom_command(TARGET onnx_bert_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${HIRAGANA_ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:onnx_bert_test>")
        endif()

        if(HIRAGANA_LLM_BACKEND AND HIRAGANA_LLM_MODEL AND EXISTS "${HIRAGANA_LLM_MODEL}")
            get_filename_component(HIRAGANA_LLM_DIR "${HIRAGANA_LLM_MODEL}" DIRECTORY)

            # The BERT tokenizer (NFKC + MeCab + WordPiece) against the
            # reference BertJapaneseTokenizer.
            add_executable(bert_tokenizer_test tests/bert_tokenizer_test.cpp)
            target_link_libraries(bert_tokenizer_test PRIVATE hiragana_llm)
            add_custom_command(TARGET bert_tokenizer_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${HIRAGANA_ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:bert_tokenizer_test>")

            # The port of llm.py (assist/assist_yougen) against the reference
            # LanguageModel.
            add_executable(llm_assist_test tests/llm_assist_test.cpp)
            target_link_libraries(llm_assist_test PRIVATE hiragana_llm)
            add_custom_command(TARGET llm_assist_test POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    "${HIRAGANA_ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
                    "$<TARGET_FILE_DIR:llm_assist_test>")

            if(HIRAGANA_MECAB_DIC_DIR AND EXISTS "${HIRAGANA_MECAB_DIC_DIR}")
                add_test(NAME llm_tokenizer_generate
                    COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_llm_tokenizer_golden.py
                        ${GOLDEN_DIR}/llm_tokenizer.txt)
                set_tests_properties(llm_tokenizer_generate PROPERTIES FIXTURES_SETUP llm_tokenizer_golden)
                add_test(NAME llm_tokenizer
                    COMMAND bert_tokenizer_test "${HIRAGANA_LLM_DIR}/vocab.txt"
                        "${HIRAGANA_MECAB_DIC_DIR}" ${GOLDEN_DIR}/llm_tokenizer.txt)
                set_tests_properties(llm_tokenizer PROPERTIES FIXTURES_REQUIRED llm_tokenizer_golden)

                add_test(NAME llm_assist_generate
                    COMMAND ${CMAKE_COMMAND} -E env PYTHONUTF8=1
                        ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden/gen_llm_assist_golden.py
                        ${GOLDEN_DIR}/llm_assist.txt)
                set_tests_properties(llm_assist_generate PROPERTIES FIXTURES_SETUP llm_assist_golden)
                add_test(NAME llm_assist
                    COMMAND llm_assist_test "${HIRAGANA_LLM_MODEL}" "${HIRAGANA_LLM_DIR}/vocab.txt"
                        "${HIRAGANA_MECAB_DIC_DIR}" "${UPSTREAM_DIR}/data" ${GOLDEN_DIR}/llm_assist.txt)
                set_tests_properties(llm_assist PROPERTIES FIXTURES_REQUIRED llm_assist_golden)

                # The same through hiragana-llm.exe and the client of the text
                # service; it needs the files that installer/get-llm.ps1 gets.
                if(HIRAGANA_BUILD_TSF AND EXISTS "$ENV{LOCALAPPDATA}/HiraganaIME/llm/model.onnx")
                    add_executable(llm_remote_test tests/llm_remote_test.cpp src/tsf/RemoteModel.cpp)
                    target_include_directories(llm_remote_test PRIVATE src/common src/tsf)
                    target_link_libraries(llm_remote_test PRIVATE hiragana_core advapi32)
                    add_dependencies(llm_remote_test hiragana-llm)
                    add_test(NAME llm_remote
                        COMMAND llm_remote_test "${CMAKE_BINARY_DIR}/src/tsf/hiragana-llm.exe"
                            ${GOLDEN_DIR}/llm_assist.txt ${UPSTREAM_DIR}/dic)
                    set_tests_properties(llm_remote PROPERTIES FIXTURES_REQUIRED llm_assist_golden)
                endif()
            endif()
        endif()
    endif()
endif()

if(WIN32)
    add_executable(furiganapad WIN32
        src/furiganapad/app/main.cpp
        src/furiganapad/app/MainWindow.cpp
        src/furiganapad/app/FuriganaView.cpp
        src/furiganapad/app/furiganapad.rc
    )
    target_include_directories(furiganapad PRIVATE src/common src/furiganapad/app)
    target_link_libraries(furiganapad PRIVATE furiganapad_core hiragana_core
        d2d1 dwrite imm32 comctl32 comdlg32 shell32 ole32 uuid)
    target_compile_definitions(furiganapad PRIVATE
        FURIGANAPAD_VERSION_W=L"0.7.2"
        FURIGANAPAD_VERSION_STR="0.7.2"
        FURIGANAPAD_VERSION_RC=0,7,2,0)
    if(MSVC)
        target_link_options(furiganapad PRIVATE /MANIFEST:NO)
    endif()
    # The documents of FuriganaPad, for Help
    add_custom_command(TARGET furiganapad POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/furiganapad/docs
            $<TARGET_FILE_DIR:furiganapad>/furiganapad-docs)
endif()

if(HIRAGANA_BUILD_TSF AND WIN32 AND EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/tsf/CMakeLists.txt)
    add_subdirectory(src/tsf)
    if(EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/src/setup/CMakeLists.txt)
        add_subdirectory(src/setup)
    endif()
    if(HIRAGANA_LLM_BACKEND)
        add_subdirectory(src/llm)
    endif()
endif()
