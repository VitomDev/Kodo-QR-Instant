# Kodo.exe, its core library and the tests. Use build.ps1, which also fetches
# the dependencies and builds the installer.
#   -DKODO_ZXING_SOURCE=<zxing-cpp source folder>
cmake_minimum_required(VERSION 3.21)
project(Kodo CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Static CRT: no Visual C++ redistributable needed.
set(CMAKE_MSVC_RUNTIME_LIBRARY MultiThreaded)

# ZXing-C++ with QR codes only, and its own encoder rather than libzint.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(ZXING_READERS ON CACHE BOOL "" FORCE)
set(ZXING_WRITERS OLD CACHE STRING "" FORCE)
set(ZXING_C_API OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_DEPENDENCIES LOCAL CACHE STRING "" FORCE)
foreach(format 1D AZTEC DATAMATRIX MAXICODE PDF417)
    set(ZXING_ENABLE_${format} OFF CACHE BOOL "" FORCE)
endforeach()
set(ZXING_ENABLE_QRCODE ON CACHE BOOL "" FORCE)
add_subdirectory(${KODO_ZXING_SOURCE} zxing EXCLUDE_FROM_ALL)

# Kodo's own code is held to stricter flags than the library.
add_compile_definitions(UNICODE _UNICODE NOMINMAX WIN32_LEAN_AND_MEAN _WIN32_WINNT=0x0A00 WINVER=0x0A00)
add_compile_options(/W4 /WX /permissive- /sdl /utf-8 /guard:cf /external:anglebrackets /external:W0)
add_link_options(/guard:cf /CETCOMPAT /OPT:REF /OPT:ICF)

add_library(kodocore STATIC
    src/core/clipboard.cpp
    src/core/geometry.cpp
    src/core/history.cpp
    src/core/hotkey.cpp
    src/core/image.cpp
    src/core/paths.cpp
    src/core/payload.cpp
    src/core/png.cpp
    src/core/qr.cpp
    src/core/settings.cpp
    src/core/text.cpp
)
target_include_directories(kodocore PUBLIC src)
# ZXing's internal headers (the QR writer) are included as system headers.
target_include_directories(kodocore SYSTEM PRIVATE ${KODO_ZXING_SOURCE}/core/src)
target_link_libraries(kodocore PUBLIC user32 advapi32 shell32 ole32 shlwapi windowscodecs PRIVATE ZXing::ZXing)

add_executable(Kodo WIN32
    src/app/generator_window.cpp
    src/app/history_window.cpp
    src/app/main.cpp
    src/app/result_popup.cpp
    src/app/screen_capture.cpp
    src/app/selection_overlay.cpp
    src/app/settings_dialog.cpp
    src/app/theme.cpp
    src/app/toast.cpp
    src/app/tray_app.cpp
    src/app/Kodo.rc
)
target_link_libraries(Kodo PRIVATE kodocore gdi32 comctl32 comdlg32 dwmapi uxtheme)
# The manifest is embedded through Kodo.rc.
target_link_options(Kodo PRIVATE /MANIFEST:NO)

add_executable(KodoTests tests/tests.cpp src/app/screen_capture.cpp)
target_link_libraries(KodoTests PRIVATE kodocore gdi32)
