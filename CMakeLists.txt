cmake_minimum_required(VERSION 3.16)
project(mmtty-qt VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets Multimedia SerialPort)

# ---------------------------------------------------------------------------
# DSP core: the original MMTTY algorithms (modulator, four demodulators, AFC
# helpers, FFT, FIR/IIR design).  Plain C++, no GUI / OS dependencies.
# ---------------------------------------------------------------------------
add_library(mmtty_dsp STATIC
    src/dsp/CLX.cpp
    src/dsp/Fft.cpp
    src/dsp/fir.cpp
    src/dsp/Rtty.cpp
    src/dsp/compat.cpp
)
target_include_directories(mmtty_dsp PUBLIC src/dsp)
if(MSVC)
    target_compile_options(mmtty_dsp PRIVATE /W3 /wd4244 /wd4267 /wd4305 /wd4996)
else()
    target_compile_options(mmtty_dsp PRIVATE -Wall -Wno-class-memaccess -Wno-char-subscripts)
endif()

# ---------------------------------------------------------------------------
# Engine (audio I/O, TX/RX state machine, PTT, macros)
# ---------------------------------------------------------------------------
add_library(mmtty_engine STATIC
    src/engine/Engine.cpp
    src/engine/Engine.h
    src/engine/FskKeyer.cpp
    src/engine/FskKeyer.h
    src/engine/PttPort.cpp
    src/engine/PttPort.h
    src/engine/Macro.cpp
    src/engine/Macro.h
    src/engine/WavFile.cpp
    src/engine/WavFile.h
)
target_include_directories(mmtty_engine PUBLIC src/engine)
find_package(Threads REQUIRED)
target_link_libraries(mmtty_engine PUBLIC mmtty_dsp Qt6::Core Qt6::Multimedia Qt6::SerialPort Threads::Threads)
if(WIN32)
    target_link_libraries(mmtty_engine PUBLIC winmm)
endif()

# ---------------------------------------------------------------------------
# Application
# ---------------------------------------------------------------------------
option(MMTTY_BUILD_APP "Build the GUI application" ON)
# MMTTY Engine Remote Mode API (N1MM Logger+ & co.).  Only active in the Windows executable (it uses window
# messages and shared memory); on other systems the option just builds the portable part for the tests.
if(WIN32)
    option(MMTTY_REMOTE_API "Build the engine remote mode API" ON)
else()
    option(MMTTY_REMOTE_API "Build the engine remote mode API (portable part, not used by the executable)" OFF)
endif()

if(MMTTY_BUILD_APP)
add_library(mmtty_ui STATIC
    src/ui/MainWindow.cpp
    src/ui/MainWindow.h
    src/ui/MainWindow.ui
    src/ui/Scopes.cpp
    src/ui/Scopes.h
    src/ui/SettingsDialog.cpp
    src/ui/SettingsDialog.h
    src/ui/SettingsDialog.ui
    src/ui/AppSettings.cpp
    src/ui/AppSettings.h
    src/ui/QsoLog.cpp
    src/ui/QsoLog.h
)
target_include_directories(mmtty_ui PUBLIC src/ui)
set_target_properties(mmtty_ui PROPERTIES AUTOUIC_SEARCH_PATHS ${CMAKE_CURRENT_SOURCE_DIR}/src/ui)
target_link_libraries(mmtty_ui PUBLIC mmtty_engine Qt6::Widgets)
if(MMTTY_REMOTE_API)
    target_sources(mmtty_ui PRIVATE
        src/remote/RemoteProtocol.cpp
        src/remote/RemoteProtocol.h
        src/remote/RemoteApi.cpp
        src/remote/RemoteApi.h
    )
    target_include_directories(mmtty_ui PUBLIC src/remote)
    if(WIN32)
        target_sources(mmtty_ui PRIVATE src/remote/RemoteWin.cpp src/remote/RemoteWin.h)
        target_link_libraries(mmtty_ui PUBLIC user32)
    endif()
    target_compile_definitions(mmtty_ui PUBLIC MMTTY_REMOTE_API=1)
endif()
if(NOT MSVC)
    target_compile_options(mmtty_ui PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_executable(mmtty src/main.cpp resources/mmtty.qrc)
target_include_directories(mmtty PRIVATE src)
target_link_libraries(mmtty PRIVATE mmtty_ui)
if(WIN32)
    set_target_properties(mmtty PROPERTIES WIN32_EXECUTABLE ON)
    target_sources(mmtty PRIVATE resources/mmtty.rc)
endif()
install(TARGETS mmtty RUNTIME DESTINATION bin)
endif()

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
include(CTest)
if(BUILD_TESTING)
    add_executable(dsp_loopback tests/dsp_loopback.cpp)
    target_link_libraries(dsp_loopback PRIVATE mmtty_dsp)
    add_test(NAME dsp_loopback COMMAND dsp_loopback)

    add_executable(engine_test tests/engine_test.cpp)
    target_link_libraries(engine_test PRIVATE mmtty_engine)
    add_test(NAME engine_test COMMAND engine_test)
    set_tests_properties(engine_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(fsk_test tests/fsk_test.cpp)
    target_link_libraries(fsk_test PRIVATE mmtty_engine)
    add_test(NAME fsk_test COMMAND fsk_test)
    set_tests_properties(fsk_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

    add_executable(macro_test tests/macro_test.cpp)
    target_link_libraries(macro_test PRIVATE mmtty_engine)
    add_test(NAME macro_test COMMAND macro_test)

    if(MMTTY_BUILD_APP AND MMTTY_REMOTE_API)
        add_executable(remote_test tests/remote_test.cpp)
        target_link_libraries(remote_test PRIVATE mmtty_ui)
        add_test(NAME remote_test COMMAND remote_test)
        set_tests_properties(remote_test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
    endif()

    if(MMTTY_BUILD_APP)
        add_executable(gui_smoke tests/gui_smoke.cpp)
        target_link_libraries(gui_smoke PRIVATE mmtty_ui)
        add_test(NAME gui_smoke COMMAND gui_smoke)
        set_tests_properties(gui_smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 180)
    endif()
endif()
