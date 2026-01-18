cmake_minimum_required(VERSION 3.16)  # Minimum for Qt 6 support

project(AVA VERSION 1.0 LANGUAGES CXX)  # Project name

set(CMAKE_CXX_STANDARD 17)  # Qt 6 recommends C++17
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)  # Enable automatic moc for Qt
set(CMAKE_AUTORCC ON)  # Enable automatic rcc for resources
set(CMAKE_AUTOUIC ON)  # Enable automatic uic for UI files

find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets)  # Add MultimediaWidgets here

add_executable(AVA main.cpp MainWindow.cpp)  # Your sources

target_link_libraries(AVA PRIVATE Qt6::Widgets Qt6::Multimedia Qt6::MultimediaWidgets)  # Add Qt6::MultimediaWidgets here