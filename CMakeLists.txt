cmake_minimum_required(VERSION 3.10)
project(hw_test LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

if(NOT DEFINED OE_QMAKE_PATH_EXTERNAL_HOST_BINS AND DEFINED ENV{OE_QMAKE_PATH_EXTERNAL_HOST_BINS})
  set(OE_QMAKE_PATH_EXTERNAL_HOST_BINS "$ENV{OE_QMAKE_PATH_EXTERNAL_HOST_BINS}")
endif()

find_package(Qt5 REQUIRED COMPONENTS Widgets Core Gui)

add_executable(hw-test
  src/main.cpp
  src/mainwindow.cpp
  src/mainwindow.h
  src/cameraview.cpp
  src/cameraview.h
  src/commtest.cpp
  src/commtest.h
)

target_link_libraries(hw-test
  Qt5::Widgets
  Qt5::Core
  Qt5::Gui
)

if(WIN32)
    target_link_libraries(hw-test ws2_32)
    target_compile_definitions(hw-test PRIVATE WIN32_LEAN_AND_MEAN NOMINMAX)
endif()

target_include_directories(hw-test PRIVATE src)
