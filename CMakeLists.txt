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
# Le "RICOH Camera USB SDK for C++" (645Z, K-1, K-1 II, KP) n'est plus
# distribué par Ricoh (portail api.ricoh fermé). Le dépôt vendorise donc la
# copie redistribuable du projet INDI (third_party/libricohcamerasdk :
# en-têtes officiels v1.1.0 + libs Linux x64/x86/armv7l, licence Ricoh §2.1
# autorisant la distribution avec l'application).
#   - Linux : le SDK vendorisé est utilisé automatiquement.
#   - Autre plateforme ou autre copie du SDK : -DRICOH_CAMERA_SDK_ROOT=<racine>.
#   - macOS/Windows sans SDK : backend libgphoto2 (voir README).
set(RICOH_CAMERA_SDK_ROOT "" CACHE PATH
    "Racine du RICOH Camera USB SDK C++ (active la communication native IT2)")

set(_ricoh_root "${RICOH_CAMERA_SDK_ROOT}")
set(_ricoh_vendored "${CMAKE_CURRENT_SOURCE_DIR}/third_party/libricohcamerasdk")

if(NOT _ricoh_root AND CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|amd64)$")
        set(_ricoh_arch "x64")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^i[3-6]86$")
        set(_ricoh_arch "x86")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^armv7")
        set(_ricoh_arch "armv7l")
    endif()

    if(DEFINED _ricoh_arch)
        # Les .bin d'INDI sont des .so : on les met au nom attendu par l'éditeur
        # de liens dans le répertoire de build.
        set(_ricoh_libdir "${CMAKE_BINARY_DIR}/ricoh_sdk_lib")
        file(MAKE_DIRECTORY "${_ricoh_libdir}")
        file(COPY_FILE "${_ricoh_vendored}/lib/${_ricoh_arch}/libRicohCameraSDKCpp.bin"
                       "${_ricoh_libdir}/libRicohCameraSDKCpp.so" ONLY_IF_DIFFERENT)
        file(COPY_FILE "${_ricoh_vendored}/lib/${_ricoh_arch}/libmtpricoh.bin"
                       "${_ricoh_libdir}/libmtpricoh.so.9" ONLY_IF_DIFFERENT)

        target_compile_definitions(PentaxTether PRIVATE PENTAX_WITH_RICOH_SDK=1)
        target_include_directories(PentaxTether SYSTEM PRIVATE "${_ricoh_vendored}/include")
        # libmtpricoh est lié explicitement : le RUNPATH de l'exécutable ne
        # s'applique pas aux dépendances transitives de la lib SDK.
        target_link_libraries(PentaxTether PRIVATE
            "${_ricoh_libdir}/libRicohCameraSDKCpp.so"
            "${_ricoh_libdir}/libmtpricoh.so.9")
        set_target_properties(PentaxTether PROPERTIES
            BUILD_RPATH "${_ricoh_libdir}"
            INSTALL_RPATH "$ORIGIN")
        # DT_RPATH (et non RUNPATH) : le chemin vaut aussi pour la dépendance
        # transitive libmtpricoh.so.9 de la lib SDK.
        target_link_options(PentaxTether PRIVATE "-Wl,--disable-new-dtags")
        message(STATUS "Backend caméra : RICOH Camera USB SDK vendorisé (${_ricoh_arch})")
        set(_ricoh_done TRUE)
    endif()
endif()

if(_ricoh_root)
    find_library(RICOH_SDK_LIBRARY
        NAMES RicohCameraSDKCpp ricoh_camera_sdk
        PATHS "${_ricoh_root}/lib" "${_ricoh_root}/bin"
        NO_DEFAULT_PATH)

    if(NOT RICOH_SDK_LIBRARY)
        message(FATAL_ERROR "RICOH_CAMERA_SDK_ROOT est défini mais la librairie "
                            "RicohCameraSDKCpp est introuvable sous ${_ricoh_root}/lib")
    endif()

    target_compile_definitions(PentaxTether PRIVATE PENTAX_WITH_RICOH_SDK=1)
    target_include_directories(PentaxTether SYSTEM PRIVATE "${_ricoh_root}/include")
    target_link_libraries(PentaxTether PRIVATE ${RICOH_SDK_LIBRARY})
    message(STATUS "Backend caméra : RICOH Camera USB SDK (${RICOH_SDK_LIBRARY})")
elseif(NOT _ricoh_done)
    message(STATUS "Backend caméra : libgphoto2 (SDK Ricoh indisponible sur cette plateforme)")
endif()
