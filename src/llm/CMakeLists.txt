# hiragana-llm.exe: the server of the LLM-assisted conversion (64-bit only).
add_executable(hiragana-llm WIN32 main.cpp hiragana-llm.rc)
target_include_directories(hiragana-llm PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
target_link_libraries(hiragana-llm PRIVATE hiragana_llm shell32 ole32 advapi32)
# Next to hiragana-ime.dll, which starts it.
set_target_properties(hiragana-llm PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/src/tsf)
add_custom_command(TARGET hiragana-llm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${HIRAGANA_ONNXRUNTIME_DIR}/lib/onnxruntime.dll"
        $<TARGET_FILE_DIR:hiragana-llm>
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:hiragana-llm>/data
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${UPSTREAM_DIR}/data/yougen_token.dic
        ${UPSTREAM_DIR}/data/katuyou_token.dic
        ${UPSTREAM_DIR}/data/kango_token.json
        $<TARGET_FILE_DIR:hiragana-llm>/data)
target_compile_definitions(hiragana-llm PRIVATE
    HIRAGANA_VERSION_STR="${PROJECT_VERSION}"
    HIRAGANA_VERSION_RC=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${PROJECT_VERSION_TWEAK})
# The script that gets the model and unidic-lite (see the setup program).
add_custom_command(TARGET hiragana-llm POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_SOURCE_DIR}/installer/get-llm.ps1
        ${CMAKE_SOURCE_DIR}/tools/export-llm-model.py
        $<TARGET_FILE_DIR:hiragana-llm>)
