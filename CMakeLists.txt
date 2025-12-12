cmake_minimum_required(VERSION 3.20)

if(CMAKE_GENERATOR MATCHES "Visual Studio")
    set(_vs_output_dir "${CMAKE_BINARY_DIR}/")
    list(APPEND CMAKE_VS_GLOBALS
        "BaseOutputPath=${_vs_output_dir}"
        "BaseIntermediateOutputPath=${_vs_output_dir}"
    )
endif()

project(WinMacMenu C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Enable Unicode
add_definitions(-DUNICODE -D_UNICODE -DWIN32_LEAN_AND_MEAN -D_CRT_SECURE_NO_WARNINGS)
add_definitions(-D_WIN32_WINNT=0x0A00 -DCOBJMACROS)

add_executable(WinMacMenu WIN32
    src/main.c
    src/menu.c
    src/config.c
    src/theme.c
    src/recent.c
    src/util.c
    src/settings.c
    src/controls.c
    src/taskbar_hook.c
    WinMacMenu.rc
)

# Hidden / experimental modern owner-draw style (disabled by default)
option(ENABLE_MODERN_STYLE "Enable modern owner-draw menu style" OFF)
if(ENABLE_MODERN_STYLE)
    target_compile_definitions(WinMacMenu PRIVATE ENABLE_MODERN_STYLE=1)
endif()

# Include directories
target_include_directories(WinMacMenu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

# Link necessary Windows libraries
# Note: powrprof for sleep, dwmapi/uxtheme for dark mode hints, ole32/shell32 for COM & shell
set(LINK_LIBS
    user32
    gdi32
    comctl32
    shell32
    shlwapi
    ole32
    uuid
    uxtheme
    dwmapi
    powrprof
    advapi32
    version
)

target_link_libraries(WinMacMenu PRIVATE ${LINK_LIBS})

# Favor smallest, GUI app
if(MSVC)
    target_compile_options(WinMacMenu PRIVATE /W3 /MP)
    # Eliminate console in Release and RelWithDebInfo (WIN32 already suppresses console)
endif()

install(TARGETS WinMacMenu RUNTIME DESTINATION .)
