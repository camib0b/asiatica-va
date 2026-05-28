cmake_minimum_required(VERSION 3.16)

project(AVA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network)

qt_standard_project_setup()

qt_add_executable(AVA
  main.cpp
  i18n/AppLocale.cpp
  i18n/LocaleNotifier.cpp
  style/theme.cpp
  ui/MainWindow.cpp
  ui/WelcomeWindow.cpp
  ui/GameSetupWindow.cpp
  ui/WorkWindow.cpp
  ui/ClipDurationSettingsDialog.cpp
  ui/XmlSyncDialog.cpp
  ui/XmlEventMappingDialog.cpp
  ui/StatsWindow.cpp
  state/EventDefaults.cpp
  state/EventCodeMap.cpp
  state/TagSession.cpp
  components/VideoControlsBar.cpp
  components/TimelineBar.cpp
  components/GameControls.cpp
  components/VideoPlayer.cpp
  export/ClipExporter.cpp
  export/ClipTrimBar.cpp
  export/ExportDialog.cpp
  export/VideoConcatenator.cpp
  export/PlaybackVideoPreparer.cpp
  export/XmlExporter.cpp
  export/XmlImporter.cpp
  export/YouTubeConfig.cpp
  export/YouTubeAuthManager.cpp
  export/YouTubeUploader.cpp
)

# macOS app bundle and Dock icon
if(APPLE)
  target_sources(AVA PRIVATE macos/PlaybackActivity.mm macos/SystemPowerNotifier.mm)
  set_source_files_properties(macos/PlaybackActivity.mm macos/SystemPowerNotifier.mm
                              PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_link_libraries(AVA PRIVATE "-framework Foundation" "-framework AppKit")
  set_target_properties(AVA PROPERTIES MACOSX_BUNDLE TRUE)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
    set_target_properties(AVA PROPERTIES MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set(app_icon_src "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
    set_source_files_properties(${app_icon_src} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(AVA PRIVATE ${app_icon_src})
  endif()
endif()

qt_add_resources(AVA ava_resources
  PREFIX "/"
  FILES
    ava-icon.png
    style/theme_light.qss
)


target_include_directories(AVA PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/components
  ${CMAKE_CURRENT_SOURCE_DIR}/style
  ${CMAKE_CURRENT_SOURCE_DIR}/state
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n
  ${CMAKE_CURRENT_SOURCE_DIR}/export
)

target_link_libraries(AVA PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Network
)

set(YOUTUBE_OAUTH_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/youtube_oauth.json")
if(EXISTS "${YOUTUBE_OAUTH_CONFIG}")
  add_custom_command(TARGET AVA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YOUTUBE_OAUTH_CONFIG}"
      "$<TARGET_FILE_DIR:AVA>/youtube_oauth.json"
    COMMENT "Copying YouTube OAuth config into app bundle")
endif()
