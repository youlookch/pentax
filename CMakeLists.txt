# ============================================================================
#  Pentax 645Z Tether — application de prise de vue connectée (tethering)
#  C++17 / JUCE 8 / libgphoto2
#
#  macOS  : clang + Homebrew (brew install libgphoto2 pkg-config cmake ninja)
#  Windows: MSYS2 / MinGW-w64 (libgphoto2 n'est pas compilable avec MSVC,
#           voir README.md § Compilation Windows)
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(PentaxTether VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- JUCE 8 : récupéré automatiquement à la configuration -------------------
include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        8.0.8
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(JUCE)

# --- libgphoto2 : localisée via pkg-config ----------------------------------
# macOS  : fourni par `brew install libgphoto2`
# MSYS2  : fourni par `pacman -S mingw-w64-x86_64-libgphoto2`
find_package(PkgConfig REQUIRED)
pkg_check_modules(GPHOTO2 REQUIRED IMPORTED_TARGET libgphoto2)

# --- Application ------------------------------------------------------------
juce_add_gui_app(PentaxTether
    PRODUCT_NAME   "Pentax 645Z Tether"
    COMPANY_NAME   "OpenTether"
    BUNDLE_ID      "com.opentether.pentax645z"
    ICON_BIG       ""
)

target_sources(PentaxTether PRIVATE
    Source/Main.cpp
    Source/CameraController.cpp
    Source/MainComponent.cpp
)

target_compile_definitions(PentaxTether PRIVATE
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:PentaxTether,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:PentaxTether,JUCE_VERSION>"
)

target_link_libraries(PentaxTether
    PRIVATE
        PkgConfig::GPHOTO2
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags
)

# libgphoto2 est une lib C : ses en-têtes déclenchent des warnings avec les
# flags stricts de JUCE ; on les inclut donc en "system includes".
target_include_directories(PentaxTether SYSTEM PRIVATE ${GPHOTO2_INCLUDE_DIRS})
