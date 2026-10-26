cmake_minimum_required(VERSION 3.25)
project(RePS3Native VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent Network)
find_package(Freetype QUIET)
if(NOT Freetype_FOUND)
    find_package(Qt6BundledFreetype CONFIG REQUIRED)
    find_package(Qt6FreetypePrivate CONFIG REQUIRED)
endif()
qt_add_executable(re-ps3-native
    src/main.cpp src/assets.cpp src/assets.h src/p3t.cpp src/p3t.h src/qrc.cpp src/qrc.h src/catalog.cpp src/catalog.h
    src/preferences.cpp src/preferences.h
    src/media_library.cpp src/media_library.h src/settings_menu.cpp
    src/playback_queue.cpp src/playback_queue.h
    src/photo_viewport.cpp src/photo_viewport.h
    src/interaction_audio.cpp src/interaction_audio.h
    src/text_renderer.cpp src/text_renderer.h
    src/font_raster.cpp src/font_raster.h
    src/profiles.cpp src/profiles.h
    src/steam_library.cpp src/steam_library.h src/steam_friends.cpp src/steam_friends.h
    src/steam_artwork.cpp src/steam_artwork.h
    src/content_registry.cpp src/content_registry.h
    src/rsx_program.cpp src/rsx_program.h src/ribbon.cpp src/ribbon.h src/hdr.cpp src/hdr.h src/icons.cpp src/icons.h src/particles.cpp src/particles.h src/coldboot.cpp src/coldboot.h
    src/shell.cpp src/shell.h src/host.cpp src/host.h src/pad.cpp src/pad.h src/view.cpp src/view.h)
target_link_libraries(re-ps3-native PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent Qt6::Network)
if(Freetype_FOUND)
    target_link_libraries(re-ps3-native PRIVATE Freetype::Freetype)
else()
    target_link_libraries(re-ps3-native PRIVATE Qt6::BundledFreetype Qt6::FreetypePrivate)
endif()
qt_add_executable(xmb-reference-frames src/reference_frames.cpp)
target_link_libraries(xmb-reference-frames PRIVATE Qt6::Gui Qt6::Multimedia)
if(WIN32)
    target_link_libraries(re-ps3-native PRIVATE xinput9_1_0 hid setupapi ole32 iphlpapi ws2_32)
endif()
if(MSVC)
    target_compile_options(re-ps3-native PRIVATE /W4 /permissive- /utf-8 /Zi)
    # Program database for the crash handler's stack traces (main.cpp); optimisation is unchanged.
    target_link_options(re-ps3-native PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(re-ps3-native PRIVATE dbghelp)
endif()
