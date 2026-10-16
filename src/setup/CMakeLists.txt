add_executable(hiragana-setup WIN32 main.cpp setup.rc)
target_link_libraries(hiragana-setup PRIVATE hiragana_core ole32 shell32 advapi32 user32)
# The manifest is embedded by setup.rc.
if(MSVC)
    target_link_options(hiragana-setup PRIVATE /MANIFEST:NO)
endif()
