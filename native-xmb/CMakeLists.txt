cmake_minimum_required(VERSION 3.25)
project(RePS3Native VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent Network)
qt_add_executable(re-ps3-native
    src/main.cpp src/assets.cpp src/assets.h src/qrc.cpp src/qrc.h src/catalog.cpp src/catalog.h
    src/preferences.cpp src/preferences.h
    src/media_library.cpp src/media_library.h src/settings_menu.cpp
    src/interaction_audio.cpp src/interaction_audio.h
    src/profiles.cpp src/profiles.h
    src/rsx_program.cpp src/rsx_program.h src/ribbon.cpp src/ribbon.h src/hdr.cpp src/hdr.h src/particles.cpp src/particles.h
    src/shell.cpp src/shell.h src/host.cpp src/host.h src/view.cpp src/view.h)
target_link_libraries(re-ps3-native PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent Qt6::Network)
qt_add_executable(xmb-reference-frames src/reference_frames.cpp)
target_link_libraries(xmb-reference-frames PRIVATE Qt6::Gui Qt6::Multimedia)
if(WIN32)
    target_link_libraries(re-ps3-native PRIVATE xinput9_1_0 ole32 iphlpapi ws2_32)
endif()
if(MSVC)
    target_compile_options(re-ps3-native PRIVATE /W4 /permissive- /utf-8)
endif()
