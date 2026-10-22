cmake_minimum_required(VERSION 3.16)

project(AVA LANGUAGES CXX)

set(AVA_APP_VERSION "0.1.0")

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
  ui/TagsTableModel.cpp
  ui/ClipDurationSettingsDialog.cpp
  ui/XmlSyncDialog.cpp
  ui/XmlEventMappingDialog.cpp
  ui/StatsWindow.cpp
  ui/ReplaceVideoConfirmation.cpp
  state/EventDefaults.cpp
  state/EventCatalog.cpp
  state/EventCodeMap.cpp
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
  export/ClipExporter.cpp
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
  set_target_properties(AVA PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "AVA"
    MACOSX_BUNDLE_GUI_IDENTIFIER "cl.camilaescudero.ava"
    MACOSX_BUNDLE_BUNDLE_VERSION "${AVA_APP_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${AVA_APP_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Camila Escudero"
    MACOSX_BUNDLE_INFO_STRING "AVA field-hockey video analysis"
  )
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
if(AVA_GIT_SHA STREQUAL "")
  set(AVA_VERSION_STRING "${AVA_APP_VERSION}")
else()
  set(AVA_VERSION_STRING "${AVA_APP_VERSION}+${AVA_GIT_SHA}")
endif()

target_compile_definitions(AVA PRIVATE
  AVA_VERSION_STRING="${AVA_VERSION_STRING}"
)

enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(AVAExportTests
  tests/CompilationSidecarTests.cpp
  tests/EventCatalogTests.cpp
  tests/InteractionGuardTests.cpp
  tests/PresentationPlaybackTests.cpp
  tests/VideoClipSequenceTests.cpp
  tests/ShootoutStateTests.cpp
  tests/WorkStateTests.cpp
  tests/TaggingToastTests.cpp
  tests/ModeChromeTests.cpp
  tests/PresentationFilterTests.cpp
  tests/XmlEventMappingDialogTests.cpp
  tests/UiPolicyTests.cpp
  tests/WorkDataViewTests.cpp
  tests/WorkWindowCorrectnessTests.cpp
  ui/PresentationPanel.cpp
  ui/PresentationInstancesModel.cpp
  ui/XmlEventMappingDialog.cpp
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
  ui/XmlSyncDialog.cpp
  components/TeamColorPicker.cpp
  components/TagToastOverlay.cpp
  components/VideoPlayer.cpp
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
  state/EventCodeMap.cpp
  state/TagSession.cpp
  components/FollowUpCatalog.cpp
  components/GameControls.cpp
  style/Motion.cpp
  state/PresentationQueue.cpp
  style/TextFit.cpp
  ui/ReplaceVideoConfirmation.cpp
  export/ClipExporter.cpp
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
  Qt6::Test
)

target_compile_definitions(AVAExportTests PRIVATE
  AVA_VERSION_STRING="${AVA_VERSION_STRING}"
  AVA_THEME_QSS_PATH="${CMAKE_CURRENT_SOURCE_DIR}/style/theme_light.qss"
)

add_test(NAME AVAExportTests COMMAND AVAExportTests)
