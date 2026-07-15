cmake_minimum_required(VERSION 3.16)
project(deepin-awareness-dbus-test VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

find_package(QT NAMES Qt6 Qt5 COMPONENTS Core DBus Widgets REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core DBus Widgets REQUIRED)

add_executable(deepin-awareness-dbus-test
    main.cpp
    DBusTestWindow.cpp
)

target_include_directories(deepin-awareness-dbus-test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(deepin-awareness-dbus-test PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Widgets
)
