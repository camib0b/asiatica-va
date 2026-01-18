cmake_minimum_required(VERSION 3.16)

project(AVA LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)

qt_standard_project_setup()

qt_add_executable(AVA
  main.cpp
  MainWindow.cpp
  MainWindow.h
)

target_link_libraries(AVA PRIVATE
  Qt6::Widgets
  Qt6::Multimedia
  Qt6::MultimediaWidgets
)
