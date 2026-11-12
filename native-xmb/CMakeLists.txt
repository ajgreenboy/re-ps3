cmake_minimum_required(VERSION 3.25)
project(RePS3Native VERSION 0.2.0 LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 6.8 REQUIRED COMPONENTS Widgets OpenGLWidgets Multimedia Concurrent Network)
find_package(Freetype QUIET)
find_package(Qt6ZlibPrivate CONFIG REQUIRED)
if(NOT Freetype_FOUND)
    find_package(Qt6BundledFreetype CONFIG REQUIRED)
    find_package(Qt6FreetypePrivate CONFIG REQUIRED)
endif()
qt_add_executable(re-ps3-native
    src/main.cpp src/assets.cpp src/assets.h src/p3t.cpp src/p3t.h src/qrc.cpp src/qrc.h src/catalog.cpp src/catalog.h
    src/gtf_decode.cpp src/gtf_decode.h
    src/edge_decode.cpp src/edge_decode.h
    src/raf_render.cpp src/raf_render.h
    src/vsmx_eval.cpp src/vsmx_eval.h
    src/rco_objects.cpp src/rco_objects.h
    src/osk_layout.cpp src/osk_layout.h
    src/xmbml_view.cpp src/xmbml_view.h
    src/preferences.cpp src/preferences.h
    src/release_index.cpp src/release_index.h src/real_debrid.cpp src/real_debrid.h src/release_source.cpp src/release_source.h src/flare_solverr.cpp src/flare_solverr.h src/hydra_library.cpp src/hydra_library.h src/archive_extract.cpp src/archive_extract.h
    src/phone_remote.cpp src/phone_remote.h src/phone_remote_shell.cpp
    src/media_library.cpp src/media_library.h src/settings_menu.cpp
    src/playback_queue.cpp src/playback_queue.h
    src/musicvis.cpp src/musicvis.h
    src/playlists.cpp src/playlists.h
    src/save_data.cpp src/save_data.h
    src/spotify_client.cpp src/spotify_client.h src/spotify_shell.cpp
    src/discord_client.cpp src/discord_client.h
    src/glyphs.cpp src/glyphs.h
    src/dlna_client.cpp src/dlna_client.h src/dlna_shell.cpp
    src/steam_activity.cpp src/steam_activity.h
    src/service_icons.cpp src/service_icons.h
    src/service_installer.cpp src/service_installer.h
    src/tailscale_client.cpp src/tailscale_client.h src/tailscale_shell.cpp
    src/system_power.cpp src/system_power.h
    src/boot_prefetch.cpp src/boot_prefetch.h
    src/file_ops.cpp src/file_ops.h
    src/weather_client.cpp src/weather_client.h src/weather_shell.cpp
    src/self_check.cpp src/self_check.h src/system_check_shell.cpp
    src/updater.cpp src/updater.h src/updater_shell.cpp
    src/firmware_fetcher.cpp src/firmware_fetcher.h
    src/firmware_extract.cpp src/firmware_extract.h src/firmware_crypto.cpp src/firmware_crypto.h
    src/wifi_client.cpp src/wifi_client.h src/wifi_shell.cpp
    src/bluetooth_client.cpp src/bluetooth_client.h src/bluetooth_shell.cpp
    src/wol_client.cpp src/wol_client.h src/homeassistant_client.cpp src/homeassistant_client.h src/home_shell.cpp
    src/photo_viewport.cpp src/photo_viewport.h
    src/gallery_params.cpp src/gallery_params.h src/gallery_model.cpp src/gallery_model.h src/gallery_thumbs.cpp src/gallery_thumbs.h
    src/gallery_controller.cpp src/gallery_controller.h src/gallery_scene.cpp src/gallery_scene.h src/gallery_chrome.cpp src/gallery_view.h src/gallery_replay.cpp
    src/interaction_audio.cpp src/interaction_audio.h
    src/notify_overlay.cpp src/notify_overlay.h
    src/live_overlay.cpp src/live_overlay.h
    src/text_renderer.cpp src/text_renderer.h
    src/font_raster.cpp src/font_raster.h
    src/profiles.cpp src/profiles.h
    src/steam_library.cpp src/steam_library.h
    src/steam_downloads.cpp src/steam_downloads.h src/steam_accounts.cpp src/steam_accounts.h src/steam_friends.cpp src/steam_friends.h
    src/steam_artwork.cpp src/steam_artwork.h src/steam_store.cpp src/steam_store.h src/steam_owned.cpp src/steam_owned.h src/steam_collections.cpp src/steam_collections.h src/steam_overrides.cpp src/steam_overrides.h src/steam_family.cpp src/steam_family.h
    src/content_registry.cpp src/content_registry.h src/web_launch.cpp src/web_launch.h src/app_editor.cpp src/app_editor.h
    src/rawg_client.cpp src/rawg_client.h src/rawg_artwork.cpp src/rawg_artwork.h
    src/artwork_settings.cpp src/artwork_settings.h
    src/media_controls.cpp src/media_controls.h
    src/media_keys.cpp src/media_keys.h
    src/subtitles.cpp src/subtitles.h
    src/image_subtitles.cpp src/image_subtitles.h
    src/jellyfin_client.cpp src/jellyfin_client.h src/jellyfin_stream.cpp src/jellyfin_stream.h
    src/jellyfin_shell.cpp
    src/rsx_program.cpp src/rsx_program.h src/ribbon.cpp src/ribbon.h src/uniform_cache.cpp src/uniform_cache.h src/render_gate.cpp src/render_gate.h src/idle_profile.h src/hdr.cpp src/hdr.h src/icons.cpp src/icons.h src/particles.cpp src/particles.h src/coldboot.cpp src/coldboot.h
    src/setup.cpp src/setup.h src/setup_shell.cpp src/release_shell.cpp
    src/autostart.cpp src/autostart.h src/autologon.cpp src/autologon.h src/cec.cpp src/cec.h src/display_mode.cpp src/display_mode.h src/display_spec.h src/diagnostics.cpp src/diagnostics.h src/game_capture.cpp src/game_capture.h src/elevation.cpp src/elevation.h src/text_entry.cpp src/text_entry.h src/shell.cpp src/shell.h src/host.cpp src/host.h src/pad.cpp src/pad.h src/ps_watcher.cpp src/ps_watcher.h src/desktop_mode.cpp src/desktop_mode.h src/desktop_pad.cpp src/desktop_pad_win.cpp src/desktop_pad.h src/view.cpp src/view.h src/dialogs.cpp)
target_link_libraries(re-ps3-native PRIVATE Qt6::Widgets Qt6::OpenGLWidgets Qt6::Multimedia Qt6::Concurrent Qt6::Network)
target_link_libraries(re-ps3-native PRIVATE Qt6::ZlibPrivate)
option(XMB_FIRMWARE_BUILD_TESTS "Build firmware synthetic fixture tests" OFF)
option(XMB_RELEASE_BUILD_TESTS "Build release index and source backend fixture tests" OFF)
if(XMB_RELEASE_BUILD_TESTS)
    qt_add_executable(xmb-release-tests tests/release_tests.cpp src/release_index.cpp src/real_debrid.cpp src/release_source.cpp src/flare_solverr.cpp src/flare_solverr.h src/hydra_library.cpp src/hydra_library.h src/preferences.cpp src/preferences.h)
    target_include_directories(xmb-release-tests PRIVATE src)
    target_link_libraries(xmb-release-tests PRIVATE Qt6::Core Qt6::Network)
endif()
if(XMB_FIRMWARE_BUILD_TESTS)
    qt_add_executable(xmb-firmware-tests tests/firmware_tests.cpp src/firmware_extract.cpp src/firmware_crypto.cpp)
    target_include_directories(xmb-firmware-tests PRIVATE src)
    target_compile_definitions(xmb-firmware-tests PRIVATE XMB_FIRMWARE_NO_EMBEDDED_KEYS)
    target_link_libraries(xmb-firmware-tests PRIVATE Qt6::Core Qt6::ZlibPrivate)
endif()
qt_add_executable(xmb-updater updater/updater_main.cpp src/updater.cpp src/updater.h)
target_include_directories(xmb-updater PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(xmb-updater PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Network)
target_compile_definitions(xmb-updater PRIVATE XMB_APP_VERSION="${PROJECT_VERSION}")
option(XMB_UPDATER_BUILD_TESTS "Build the updater fixture runner" OFF)
if(XMB_UPDATER_BUILD_TESTS)
    qt_add_executable(xmb-updater-tests tests/updater_tests.cpp src/updater.cpp src/updater.h)
    target_include_directories(xmb-updater-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(xmb-updater-tests PRIVATE Qt6::Core Qt6::Network)
endif()
option(XMB_SETUP_BUILD_TESTS "Build the first-run setup test runner" OFF)
if(XMB_SETUP_BUILD_TESTS)
    qt_add_executable(xmb-setup-tests tests/setup_tests.cpp src/setup.cpp src/setup.h)
    target_include_directories(xmb-setup-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(xmb-setup-tests PRIVATE Qt6::Core)
endif()
option(XMB_AUTOLOGON_BUILD_TESTS "Build the automatic sign-in mapping tests" OFF)
if(XMB_AUTOLOGON_BUILD_TESTS)
    qt_add_executable(xmb-autologon-tests tests/autologon_tests.cpp src/autologon.cpp src/autologon.h)
    target_include_directories(xmb-autologon-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(xmb-autologon-tests PRIVATE Qt6::Core)
endif()
option(XMB_STEAM_VDF_BUILD_TESTS "Build the Steam KeyValues (VDF) golden tests" OFF)
if(XMB_STEAM_VDF_BUILD_TESTS)
    qt_add_executable(xmb-steam-vdf-tests tests/steam_vdf_tests.cpp
        src/steam_library.cpp src/steam_library.h src/steam_accounts.cpp src/steam_accounts.h)
    target_include_directories(xmb-steam-vdf-tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_compile_definitions(xmb-steam-vdf-tests PRIVATE XMB_VDF_GOLDEN="${CMAKE_CURRENT_SOURCE_DIR}/tests/steam_vdf_golden.txt")
    target_link_libraries(xmb-steam-vdf-tests PRIVATE Qt6::Core)
endif()
if(Freetype_FOUND)
    target_link_libraries(re-ps3-native PRIVATE Freetype::Freetype)
else()
    target_link_libraries(re-ps3-native PRIVATE Qt6::BundledFreetype Qt6::FreetypePrivate)
endif()
qt_add_executable(xmb-reference-frames src/reference_frames.cpp)
target_link_libraries(xmb-reference-frames PRIVATE Qt6::Gui Qt6::Multimedia)
if(WIN32)
    target_link_libraries(re-ps3-native PRIVATE xinput9_1_0 hid setupapi ole32 iphlpapi ws2_32 shell32)
endif()
if(MSVC)
    target_compile_options(re-ps3-native PRIVATE /W4 /permissive- /utf-8 /Zi)
    # The env-driven debug logs and the crash handler use fopen/_wfopen deliberately; fopen_s adds nothing there.
    target_compile_definitions(re-ps3-native PRIVATE _CRT_SECURE_NO_WARNINGS)
    # Program database for the crash handler's stack traces (main.cpp); optimisation is unchanged.
    target_link_options(re-ps3-native PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(re-ps3-native PRIVATE dbghelp windowsapp d3d11 dxgi dcomp dwmapi shell32 wlanapi)
endif()

# XMB Manager: the standalone companion tool (CLI + Qt UI). It shares the
# output directory with re-ps3-native, so windeployqt's Qt runtime deployment
# for the main executable also serves xmb-manager.exe. The manager never
# depends on firmware and writes only to its own data directory.
qt_add_executable(xmb-manager
    manager/manager_main.cpp
    manager/manager_core.cpp manager/manager_core.h
    manager/manager_emulators.cpp manager/manager_emulators.h
    manager/manager_scanner.cpp manager/manager_scanner.h
    manager/manager_package.cpp manager/manager_package.h
    manager/manager_cli.cpp manager/manager_cli.h
    manager/manager_ui.cpp manager/manager_ui.h
    src/content_registry.cpp src/content_registry.h src/web_launch.cpp src/web_launch.h
)
target_include_directories(xmb-manager PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/manager)
target_link_libraries(xmb-manager PRIVATE Qt6::Widgets Qt6::Gui Qt6::Core)
if(MSVC)
    target_compile_options(xmb-manager PRIVATE /W4 /permissive- /utf-8 /Zi)
    target_link_options(xmb-manager PRIVATE /DEBUG /OPT:REF /OPT:ICF)
    target_link_libraries(xmb-manager PRIVATE dbghelp)
endif()

# Optional headless test runner for the manager core (fixtures + CLI verbs).
# Off by default so a normal build stays exactly the shipped targets.
option(XMB_MANAGER_BUILD_TESTS "Build the XMB Manager test runner" OFF)
if(XMB_MANAGER_BUILD_TESTS)
    qt_add_executable(xmb-manager-tests
        manager/tests/manager_tests.cpp
        manager/manager_core.cpp manager/manager_core.h
        manager/manager_emulators.cpp manager/manager_emulators.h
        manager/manager_scanner.cpp manager/manager_scanner.h
        manager/manager_package.cpp manager/manager_package.h
        src/content_registry.cpp src/content_registry.h src/web_launch.cpp src/web_launch.h
    )
    target_include_directories(xmb-manager-tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/manager)
    target_link_libraries(xmb-manager-tests PRIVATE Qt6::Gui Qt6::Core)
    if(MSVC)
        target_compile_options(xmb-manager-tests PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()

# Optional logged-in Steam client helper. SDK headers stay external; no Steam DLL is redistributed.
set(XMB_STEAMWORKS_HEADERS "" CACHE PATH "Steamworks public header directory containing isteamclient.h")
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8 AND XMB_STEAMWORKS_HEADERS)
    if(NOT EXISTS "${XMB_STEAMWORKS_HEADERS}/isteamclient.h")
        message(FATAL_ERROR "XMB_STEAMWORKS_HEADERS must contain the Steamworks public headers")
    endif()
    add_executable(xmb-steam-client src/steam_client_helper.cpp src/steam_library.cpp src/steam_library.h
    src/steam_downloads.cpp src/steam_downloads.h src/steam_accounts.cpp src/steam_accounts.h)
    target_include_directories(xmb-steam-client PRIVATE "${XMB_STEAMWORKS_HEADERS}")
    target_compile_definitions(xmb-steam-client PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN)
    target_link_libraries(xmb-steam-client PRIVATE Qt6::Core)
    if(MSVC)
        target_compile_options(xmb-steam-client PRIVATE /W4 /permissive- /utf-8)
    endif()
endif()
