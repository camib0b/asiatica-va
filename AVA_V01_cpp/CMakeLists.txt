cmake_minimum_required(VERSION 3.16)

# scripts/macos_release_common.sh reads these two lines. Keep them unindented.
set(AVA_MACOS_DEPLOYMENT_TARGET "12.0")
set(AVA_MACOS_ARCHITECTURE "arm64")

# AVA_V01_cpp/VERSION is the only app version. release_macos.sh writes it.
file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" AVA_VERSION_FILE_LINES)
set(AVA_APP_VERSION "")
set(AVA_BUILD_NUMBER "")
foreach(ava_version_line IN LISTS AVA_VERSION_FILE_LINES)
  string(STRIP "${ava_version_line}" ava_version_line)
  if(ava_version_line STREQUAL "" OR ava_version_line MATCHES "^#")
    continue()
  endif()
  if(AVA_APP_VERSION STREQUAL "")
    set(AVA_APP_VERSION "${ava_version_line}")
  elseif(AVA_BUILD_NUMBER STREQUAL "")
    set(AVA_BUILD_NUMBER "${ava_version_line}")
  else()
    message(FATAL_ERROR "VERSION has an unexpected extra line: ${ava_version_line}")
  endif()
endforeach()
if(NOT AVA_APP_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
  message(FATAL_ERROR
    "VERSION marketing version must be MAJOR.MINOR.PATCH, got '${AVA_APP_VERSION}'.")
endif()
if(NOT AVA_BUILD_NUMBER MATCHES "^[1-9][0-9]*$")
  message(FATAL_ERROR
    "VERSION build number must be a positive integer, got '${AVA_BUILD_NUMBER}'.")
endif()
message(STATUS "AVA ${AVA_APP_VERSION} build ${AVA_BUILD_NUMBER}")

# Set before project() so the Apple compiler gets -mmacosx-version-min and -arch.
# Non-Apple hosts ignore both variables.
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "${AVA_MACOS_DEPLOYMENT_TARGET}" CACHE STRING
      "Minimum macOS version for AVA")
  set(CMAKE_OSX_ARCHITECTURES "${AVA_MACOS_ARCHITECTURE}" CACHE STRING
      "AVA coach builds are Apple Silicon")
endif()

project(AVA VERSION ${AVA_APP_VERSION} LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(AVA_ICON_PNGS
  AppIcon.iconset/icon_16x16.png
  AppIcon.iconset/icon_16x16@2x.png
  AppIcon.iconset/icon_32x32.png
  AppIcon.iconset/icon_32x32@2x.png
  AppIcon.iconset/icon_128x128.png
  AppIcon.iconset/icon_128x128@2x.png
  AppIcon.iconset/icon_256x256.png
  AppIcon.iconset/icon_256x256@2x.png
  AppIcon.iconset/icon_512x512.png
  AppIcon.iconset/icon_512x512@2x.png
)
foreach(ava_icon_png IN LISTS AVA_ICON_PNGS)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${ava_icon_png}")
    message(FATAL_ERROR "Missing app icon ${ava_icon_png}")
  endif()
endforeach()

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets Network Concurrent)

qt_standard_project_setup()

qt_add_executable(AVA
  main.cpp
  i18n/AppLocale.cpp
  i18n/LocaleNotifier.cpp
  style/theme.cpp
  style/TextFit.cpp
  style/Motion.cpp
  ui/MainWindow.cpp
  ui/WelcomeWindow.cpp
  ui/GameSetupWindow.cpp
  ui/WorkWindow.cpp
  ui/PlaybackTemporaryDirectoryController.cpp
  ui/NotesController.cpp
  ui/PresentationPlaybackController.cpp
  ui/TagsTableController.cpp
  ui/TagRowDisplay.cpp
  components/TeamColorPicker.cpp
  ui/PresentationPanel.cpp
  ui/PresentationInstancesModel.cpp
  ui/PresentationSelectionBreakdown.cpp
  ui/PresentationSelectionPopup.cpp
  ui/TagsTableModel.cpp
  ui/ClipDurationSettingsDialog.cpp
  ui/GameControlsContextMenu.cpp
  ui/GameControlsEditorDialog.cpp
  ui/XmlSyncDialog.cpp
  ui/XmlDropHighlight.cpp
  ui/XmlEventMappingDialog.cpp
  ui/StatsWindow.cpp
  ui/ReplaceVideoConfirmation.cpp
  state/EventDefaults.cpp
  state/EventCatalog.cpp
  state/EventLabelOverrides.cpp
  state/EventControlsSetup.cpp
  state/EventControlsValidation.cpp
  state/EventControlsTemplateStore.cpp
  state/MatchSessionFile.cpp
  state/Utf8JsonFile.cpp
  state/EventLabelResolver.cpp
  state/EventCodeMap.cpp
  state/TagEvent.cpp
  state/PresentationQueue.cpp
  state/PresentationPlayback.cpp
  state/TagFilter.cpp
  state/TagSession.cpp
  components/VideoControlsBar.cpp
  components/PlaybackSpeedometer.cpp
  components/TimelineBar.cpp
  components/GameControls.cpp
  components/TagToastOverlay.cpp
  components/FollowUpCatalog.cpp
  components/MatchNotesEditor.cpp
  components/VideoPlayer.cpp
  components/DrawingGraphicsItem.cpp
  components/TelestrationToolBar.cpp
  components/TelestrationSurface.cpp
  state/Telestration.cpp
  export/ClipExporter.cpp
  export/ClipFilterGraph.cpp
  export/ExportVideoEncoder.cpp
  export/ShootoutState.cpp
  export/ShootoutOverlay.cpp
  export/CompilationSidecar.cpp
  export/FfmpegLocator.cpp
  export/ClipTrimBar.cpp
  export/ExportClipBuilder.cpp
  export/ExportJobManager.cpp
  export/ExportJobsBar.cpp
  export/ExportOutputPath.cpp
  export/ExportSettingsDialog.cpp
  export/ConcatFileOrderDialog.cpp
  export/VideoClipSequence.cpp
  export/VideoConcatenator.cpp
  export/PlaybackVideoPreparer.cpp
  export/XmlExporter.cpp
  export/SessionNotesWriter.cpp
  export/XmlImporter.cpp
  export/XaiConfig.cpp
  export/GameMetadataSuggester.cpp
)

# macOS app bundle and Dock icon
if(APPLE)
  target_sources(AVA PRIVATE
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    style/Motion_mac.mm
  )
  set_source_files_properties(
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    style/Motion_mac.mm
    PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_link_libraries(AVA PRIVATE
    "-framework Foundation"
    "-framework AppKit"
  )
  if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    message(FATAL_ERROR "CMAKE_OSX_DEPLOYMENT_TARGET is empty.")
  endif()
  set(AVA_ICON_ICNS "${CMAKE_CURRENT_BINARY_DIR}/AppIcon.icns")
  find_program(AVA_ICONUTIL iconutil)
  if(AVA_ICONUTIL)
    set(AVA_ICON_PNG_PATHS "")
    foreach(ava_icon_png IN LISTS AVA_ICON_PNGS)
      list(APPEND AVA_ICON_PNG_PATHS "${CMAKE_CURRENT_SOURCE_DIR}/${ava_icon_png}")
    endforeach()
    add_custom_command(
      OUTPUT "${AVA_ICON_ICNS}"
      COMMAND "${AVA_ICONUTIL}" -c icns "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.iconset" -o "${AVA_ICON_ICNS}"
      DEPENDS ${AVA_ICON_PNG_PATHS}
      COMMENT "Building AppIcon.icns"
      VERBATIM
    )
  elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
    set(AVA_ICON_ICNS "${CMAKE_CURRENT_SOURCE_DIR}/AppIcon.icns")
  else()
    message(FATAL_ERROR
      "AppIcon.icns is missing and iconutil was not found. Run scripts/make_icns.sh on a Mac.")
  endif()
  set_source_files_properties("${AVA_ICON_ICNS}" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources
    GENERATED TRUE
  )
  target_sources(AVA PRIVATE "${AVA_ICON_ICNS}")
  set_target_properties(AVA PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AVA"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cl.camilaescudero.ava"
    MACOSX_BUNDLE_COPYRIGHT "Camila Escudero"
    MACOSX_BUNDLE_ICON_FILE "AppIcon.icns"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/macos/Info.plist.in"
    OSX_ARCHITECTURES "${CMAKE_OSX_ARCHITECTURES}"
    XCODE_ATTRIBUTE_MACOSX_DEPLOYMENT_TARGET "${CMAKE_OSX_DEPLOYMENT_TARGET}"
    XCODE_ATTRIBUTE_ONLY_ACTIVE_ARCH "NO"
  )
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
  Qt6::Concurrent
)

set(AVA_GIT_SHA "")
find_package(Git QUIET)
if(Git_FOUND)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    OUTPUT_VARIABLE AVA_GIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE AVA_GIT_SHA_RESULT
  )
  if(NOT AVA_GIT_SHA_RESULT EQUAL 0)
    set(AVA_GIT_SHA "")
  endif()
endif()
set(AVA_VERSION_DISPLAY "${AVA_APP_VERSION} (${AVA_BUILD_NUMBER})")
if(AVA_GIT_SHA STREQUAL "")
  set(AVA_VERSION_STRING "${AVA_VERSION_DISPLAY}")
else()
  set(AVA_VERSION_STRING "${AVA_VERSION_DISPLAY}+${AVA_GIT_SHA}")
endif()

target_compile_definitions(AVA PRIVATE
  AVA_VERSION_STRING="${AVA_VERSION_STRING}"
)

if(APPLE)
  set_target_properties(AVA PROPERTIES
    MACOSX_BUNDLE_BUNDLE_VERSION "${AVA_BUILD_NUMBER}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AVA_APP_VERSION}"
    MACOSX_BUNDLE_LONG_VERSION_STRING "${AVA_VERSION_STRING}"
    MACOSX_BUNDLE_INFO_STRING "AVA ${AVA_VERSION_STRING}"
  )
endif()

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(AVAExportTests
  tests/CompilationSidecarTests.cpp
  tests/ExportVideoEncoderTests.cpp
  tests/FfmpegLocatorTests.cpp
  tests/EventCatalogTests.cpp
  tests/EventLabelOverrideTests.cpp
  tests/GameControlsEditorTests.cpp
  tests/TagEventRoundTripTests.cpp
  tests/InteractionGuardTests.cpp
  tests/PresentationPlaybackTests.cpp
  tests/VideoClipSequenceTests.cpp
  tests/ShootoutStateTests.cpp
  tests/WorkStateTests.cpp
  tests/TaggingToastTests.cpp
  tests/ModeChromeTests.cpp
  tests/PresentationFilterTests.cpp
  tests/PresentationSelectionBreakdownTests.cpp
  tests/XmlEventMappingDialogTests.cpp
  tests/XmlFileDropTests.cpp
  tests/UiPolicyTests.cpp
  tests/WorkDataViewTests.cpp
  tests/WorkWindowCorrectnessTests.cpp
  tests/TimelineScrubTests.cpp
  tests/PlaybackSpeedTests.cpp
  tests/TelestrationTests.cpp
  tests/MatchSessionFileTests.cpp
  tests/StateMachineTests.cpp
  ui/PresentationPanel.cpp
  ui/PresentationInstancesModel.cpp
  ui/PresentationSelectionBreakdown.cpp
  ui/PresentationSelectionPopup.cpp
  ui/XmlEventMappingDialog.cpp
  ui/XmlDropHighlight.cpp
  ui/GameSetupWindow.cpp
  ui/WorkWindow.cpp
  ui/PlaybackTemporaryDirectoryController.cpp
  ui/NotesController.cpp
  ui/PresentationPlaybackController.cpp
  ui/TagsTableController.cpp
  ui/TagRowDisplay.cpp
  ui/StatsWindow.cpp
  ui/PresentationPanel.cpp
  ui/PresentationInstancesModel.cpp
  ui/TagsTableModel.cpp
  ui/ClipDurationSettingsDialog.cpp
  ui/GameControlsContextMenu.cpp
  ui/GameControlsEditorDialog.cpp
  ui/XmlSyncDialog.cpp
  components/TeamColorPicker.cpp
  components/TagToastOverlay.cpp
  components/VideoPlayer.cpp
  components/DrawingGraphicsItem.cpp
  components/TelestrationToolBar.cpp
  components/TelestrationSurface.cpp
  components/VideoControlsBar.cpp
  components/PlaybackSpeedometer.cpp
  components/TimelineBar.cpp
  components/MatchNotesEditor.cpp
  export/GameMetadataSuggester.cpp
  export/XaiConfig.cpp
  export/ExportOutputPath.cpp
  state/PresentationPlayback.cpp
  state/TagFilter.cpp
  i18n/AppLocale.cpp
  i18n/LocaleNotifier.cpp
  state/EventDefaults.cpp
  state/EventCatalog.cpp
  state/EventLabelOverrides.cpp
  state/EventControlsSetup.cpp
  state/EventControlsValidation.cpp
  state/EventControlsTemplateStore.cpp
  state/MatchSessionFile.cpp
  state/Utf8JsonFile.cpp
  state/EventLabelResolver.cpp
  state/EventCodeMap.cpp
  state/TagEvent.cpp
  state/TagSession.cpp
  state/Telestration.cpp
  components/FollowUpCatalog.cpp
  components/GameControls.cpp
  style/Motion.cpp
  state/PresentationQueue.cpp
  style/TextFit.cpp
  ui/ReplaceVideoConfirmation.cpp
  export/ClipExporter.cpp
  export/ClipFilterGraph.cpp
  export/ExportVideoEncoder.cpp
  export/ClipTrimBar.cpp
  export/ExportJobsBar.cpp
  export/ExportSettingsDialog.cpp
  export/VideoConcatenator.cpp
  export/PlaybackVideoPreparer.cpp
  export/XmlImporter.cpp
  export/ShootoutState.cpp
  export/ShootoutOverlay.cpp
  export/CompilationSidecar.cpp
  export/ConcatFileOrderDialog.cpp
  export/ExportClipBuilder.cpp
  export/ExportJobManager.cpp
  export/FfmpegLocator.cpp
  export/SessionNotesWriter.cpp
  export/VideoClipSequence.cpp
  export/XmlExporter.cpp
)

set_target_properties(AVAExportTests PROPERTIES
  MACOSX_BUNDLE FALSE
  WIN32_EXECUTABLE FALSE
)

target_include_directories(AVAExportTests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/components
  ${CMAKE_CURRENT_SOURCE_DIR}/style
  ${CMAKE_CURRENT_SOURCE_DIR}/state
  ${CMAKE_CURRENT_SOURCE_DIR}/i18n
  ${CMAKE_CURRENT_SOURCE_DIR}/export
)

if(APPLE)
  target_sources(AVAExportTests PRIVATE
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    style/Motion_mac.mm
  )
  set_source_files_properties(
    macos/PlaybackActivity.mm
    macos/SystemPowerNotifier.mm
    style/Motion_mac.mm
    PROPERTIES COMPILE_OPTIONS -fobjc-arc)
  target_link_libraries(AVAExportTests PRIVATE
    "-framework Foundation"
    "-framework AppKit"
  )
endif()

target_link_libraries(AVAExportTests PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
  Qt6::Network
  Qt6::Concurrent
  Qt6::Test
)

target_compile_definitions(AVAExportTests PRIVATE
  AVA_VERSION_STRING="${AVA_VERSION_STRING}"
  AVA_THEME_QSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style/theme_light.qss"
)

add_test(NAME AVAExportTests COMMAND AVAExportTests)
