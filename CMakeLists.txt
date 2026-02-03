cmake_minimum_required(VERSION 3.16)

project(AVA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(AVA
  main.cpp
  style/theme.cpp
  ui/MainWindow.cpp
  ui/WelcomeWindow.cpp
  ui/WorkWindow.cpp
  ui/StatsWindow.cpp
  state/TagSession.cpp
  components/VideoControlsBar.cpp
  components/TimelineBar.cpp
  components/GameControls.cpp
  components/VideoPlayer.cpp
)

# macOS app bundle and Dock icon
if(APPLE)
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
)

target_link_libraries(AVA PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
)
