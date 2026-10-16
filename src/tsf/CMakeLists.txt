add_library(hiragana-ime SHARED
    DllMain.cpp
    Globals.cpp
    Register.cpp
    KeyMapping.cpp
    DisplayAttributes.cpp
    CandidateWindow.cpp
    LangBarButton.cpp
    TextService.cpp
    RemoteModel.cpp
    hiragana-ime.rc
    hiragana-ime.def
)
target_include_directories(hiragana-ime PRIVATE ${CMAKE_SOURCE_DIR}/src/common)
target_link_libraries(hiragana-ime PRIVATE hiragana_core ole32 oleaut32 uuid user32 gdi32 shell32 advapi32 imm32)
target_compile_definitions(hiragana-ime PRIVATE
    HIRAGANA_VERSION_W=L"${PROJECT_VERSION}"
    HIRAGANA_VERSION_STR="${PROJECT_VERSION}"
    HIRAGANA_VERSION_RC=${PROJECT_VERSION_MAJOR},${PROJECT_VERSION_MINOR},${PROJECT_VERSION_PATCH},${PROJECT_VERSION_TWEAK})
# The core library is linked into a DLL.
set_target_properties(hiragana_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

# Put the dictionaries and the layouts next to the DLL so that a development
# build works when loaded from the build directory (tools/dev-register.ps1).
add_custom_command(TARGET hiragana-ime POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${UPSTREAM_DIR}/dic $<TARGET_FILE_DIR:hiragana-ime>/dic
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${UPSTREAM_DIR}/layouts $<TARGET_FILE_DIR:hiragana-ime>/layouts)
