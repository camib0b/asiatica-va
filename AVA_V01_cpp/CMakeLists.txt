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
  ui/LicenseLockOverlay.cpp
  license/LicenseCrypto.cpp
  license/LicenseConfig.cpp
  license/LicenseManager.cpp
  components/TeamColorPicker.cpp
  ui/PresentationPanel.cpp
  ui/ClipDurationSettingsDialog.cpp
  ui/XmlSyncDialog.cpp
  ui/XmlEventMappingDialog.cpp
  ui/StatsWindow.cpp
  state/EventDefaults.cpp
  state/EventCodeMap.cpp
  state/PresentationQueue.cpp
  state/TagSession.cpp
  components/VideoControlsBar.cpp
  components/TimelineBar.cpp
  components/GameControls.cpp
  components/VideoPlayer.cpp
  export/ClipExporter.cpp
  export/ClipTrimBar.cpp
  export/ExportClipBuilder.cpp
  export/ExportJobManager.cpp
  export/ExportJobsBar.cpp
  export/ExportSettingsDialog.cpp
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
  target_sources(AVA PRIVATE
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    license/DeviceId_mac.mm
  )
  set_source_files_properties(
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    license/DeviceId_mac.mm
    PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_link_libraries(AVA PRIVATE
    "-framework Foundation"
    "-framework AppKit"
    "-framework IOKit"
    "-framework CoreFoundation"
  )
  set_target_properties(AVA PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AVA"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cl.camilaescudero.ava"
    MACOSX_BUNDLE_BUNDLE_VERSION "0.1.0"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "0.1.0"
    MACOSX_BUNDLE_COPYRIGHT "Camila Escudero"
    MACOSX_BUNDLE_INFO_STRING "AVA field-hockey video analysis"
  )
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
    set_target_properties(AVA PROPERTIES MACOSX_BUNDLE_ICON_FILE AppIcon.icns)
    set(app_icon_src "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
    set_source_files_properties(${app_icon_src} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    target_sources(AVA PRIVATE ${app_icon_src})
  endif()
else()
  target_sources(AVA PRIVATE license/DeviceId.cpp)
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
  ${CMAKE_CURRENT_SOURCE_DIR}/license
)

target_link_libraries(AVA PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Network
)

# HMAC secret used to sign trial/paid tokens. Override before shipping:
#   cmake -DAVA_LICENSE_SIGNING_SECRET="your-long-random-secret" ...
# The in-repo default is for development only; anyone with the repo can mint keys
# until Camila rebuilds with her own secret (see DISTRIBUTION.md).
if(NOT DEFINED AVA_LICENSE_SIGNING_SECRET OR AVA_LICENSE_SIGNING_SECRET STREQUAL "")
  if(DEFINED ENV{AVA_LICENSE_SIGNING_SECRET} AND NOT "$ENV{AVA_LICENSE_SIGNING_SECRET}" STREQUAL "")
    set(AVA_LICENSE_SIGNING_SECRET "$ENV{AVA_LICENSE_SIGNING_SECRET}")
  else()
    set(AVA_LICENSE_SIGNING_SECRET "ava-dev-signing-secret-not-for-production")
  endif()
endif()

if(NOT DEFINED AVA_LICENSE_API_URL)
  if(DEFINED ENV{AVA_LICENSE_API_URL})
    set(AVA_LICENSE_API_URL "$ENV{AVA_LICENSE_API_URL}")
  else()
    set(AVA_LICENSE_API_URL "")
  endif()
endif()

target_compile_definitions(AVA PRIVATE
  AVA_LICENSE_SIGNING_SECRET="${AVA_LICENSE_SIGNING_SECRET}"
  AVA_LICENSE_API_URL="${AVA_LICENSE_API_URL}"
)

set(YOUTUBE_OAUTH_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/youtube_oauth.json")
if(EXISTS "${YOUTUBE_OAUTH_CONFIG}")
  add_custom_command(TARGET AVA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${YOUTUBE_OAUTH_CONFIG}"
      "$<TARGET_FILE_DIR:AVA>/youtube_oauth.json"
    COMMENT "Copying YouTube OAuth config into app bundle")
endif()

set(LICENSE_SERVER_CONFIG "${CMAKE_CURRENT_SOURCE_DIR}/config/license_server.json")
if(EXISTS "${LICENSE_SERVER_CONFIG}")
  add_custom_command(TARGET AVA POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "${LICENSE_SERVER_CONFIG}"
      "$<TARGET_FILE_DIR:AVA>/license_server.json"
    COMMENT "Copying license server URL into app bundle")
endif()
