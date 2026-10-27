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
    Source/CameraBackendFactory.cpp
    Source/GPhoto2Backend.cpp
    Source/RicohSdkBackend.cpp
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

# --- Backend officiel Ricoh (protocole natif Image Transmitter 2) ------------
# Le "RICOH Camera USB SDK for C++" (645Z, K-1, K-1 II, KP) se télécharge sur
# https://api.ricoh (acceptation de licence : il n'est pas redistribuable ici).
# Pointer RICOH_CAMERA_SDK_ROOT vers la racine décompressée du SDK active ce
# backend à la place de libgphoto2 :
#   cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release \
#         -DRICOH_CAMERA_SDK_ROOT=$HOME/sdk/RicohCameraUSBSDK
set(RICOH_CAMERA_SDK_ROOT "" CACHE PATH
    "Racine du RICOH Camera USB SDK C++ (active la communication native IT2)")

if(RICOH_CAMERA_SDK_ROOT)
    find_library(RICOH_SDK_LIBRARY
        NAMES RicohCameraSDKCpp ricoh_camera_sdk
        PATHS "${RICOH_CAMERA_SDK_ROOT}/lib" "${RICOH_CAMERA_SDK_ROOT}/bin"
        NO_DEFAULT_PATH)

    if(NOT RICOH_SDK_LIBRARY)
        message(FATAL_ERROR "RICOH_CAMERA_SDK_ROOT est défini mais la librairie "
                            "RicohCameraSDKCpp est introuvable sous ${RICOH_CAMERA_SDK_ROOT}/lib")
    endif()

    target_compile_definitions(PentaxTether PRIVATE PENTAX_WITH_RICOH_SDK=1)
    target_include_directories(PentaxTether SYSTEM PRIVATE "${RICOH_CAMERA_SDK_ROOT}/include")
    target_link_libraries(PentaxTether PRIVATE ${RICOH_SDK_LIBRARY})
    message(STATUS "Backend caméra : RICOH Camera USB SDK (${RICOH_SDK_LIBRARY})")
else()
    message(STATUS "Backend caméra : libgphoto2 (définir RICOH_CAMERA_SDK_ROOT pour le SDK officiel)")
endif()
