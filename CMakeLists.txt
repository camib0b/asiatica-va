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
  components/VideoControlsBar.cpp
  components/TimelineBar.cpp
)

qt_add_resources(AVA ava_resources
  PREFIX "/"
  FILES
    style/theme_light.qss
)


target_include_directories(AVA PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/ui
  ${CMAKE_CURRENT_SOURCE_DIR}/components
  ${CMAKE_CURRENT_SOURCE_DIR}/style
)

target_link_libraries(AVA PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
)
