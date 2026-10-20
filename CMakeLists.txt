cmake_minimum_required(VERSION 3.16)
project(NOCOM LANGUAGES CXX)

add_library(NOCOM SHARED
    src/main.cpp
    src/game.cpp
    src/settings.cpp
    src/config.cpp
    src/render.cpp
    src/menu.cpp
    src/features.cpp
    src/esp.cpp
    src/combat_feedback.cpp
    src/entity_names.cpp
    src/netvar.cpp
    src/discord.cpp
)
target_compile_features(NOCOM PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(NOCOM PRIVATE /W3 /EHsc /O2 /D_CRT_SECURE_NO_WARNINGS)
endif()
set_target_properties(NOCOM PROPERTIES OUTPUT_NAME "NOCOM" PREFIX "" SUFFIX ".dll")
target_link_libraries(NOCOM PRIVATE user32 gdiplus comdlg32 winmm dbghelp winhttp bcrypt)
target_include_directories(NOCOM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
