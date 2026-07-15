cmake_minimum_required(VERSION 3.16)
project(deepin-environment-awareness VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Qt5/Qt6 兼容
find_package(QT NAMES Qt6 Qt5 COMPONENTS Core DBus Sql REQUIRED)
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Core DBus Sql REQUIRED)

# 测试模块（可选）
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Test QUIET)
if(NOT Qt${QT_VERSION_MAJOR}Test_FOUND)
    message(STATUS "Qt${QT_VERSION_MAJOR}Test not found, skipping tests")
endif()

# 源文件
set(CORE_SOURCES
    src/core/Event.cpp
    src/core/Config.cpp
    src/core/StorageController.cpp
    src/core/EventBus.cpp
)

set(DBUS_SOURCES
    src/dbus/DBusManager.cpp
    src/dbus/ContextAdaptor.cpp
    src/dbus/HistoryAdaptor.cpp
    src/dbus/SystemAdaptor.cpp
    src/dbus/ConfigAdaptor.cpp
    src/dbus/EventsAdaptor.cpp
)

set(UTILS_SOURCES
    src/utils/Logger.cpp
    src/utils/Redactor.cpp
)

set(SENSOR_SOURCES
    src/sensors/ISensor.cpp
)

# Daemon 可执行文件
add_executable(deepin-environment-awareness
    src/main.cpp
    ${CORE_SOURCES}
    ${DBUS_SOURCES}
    ${UTILS_SOURCES}
    ${SENSOR_SOURCES}
)

target_include_directories(deepin-environment-awareness PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(deepin-environment-awareness PRIVATE
    Qt${QT_VERSION_MAJOR}::Core
    Qt${QT_VERSION_MAJOR}::DBus
    Qt${QT_VERSION_MAJOR}::Sql
)

target_compile_definitions(deepin-environment-awareness PRIVATE
    QT_VERSION_MAJOR=${QT_VERSION_MAJOR}
)

# 安装
install(TARGETS deepin-environment-awareness
    RUNTIME DESTINATION bin
)

install(FILES data/deepin-environment-awareness.service
    DESTINATION lib/systemd/user
)

# 测试
if(Qt${QT_VERSION_MAJOR}Test_FOUND)
    enable_testing()
    add_executable(test_storage tests/test_storage.cpp ${CORE_SOURCES} ${UTILS_SOURCES})
    target_include_directories(test_storage PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_storage PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Test
    )
    add_test(NAME test_storage COMMAND test_storage)

    add_executable(test_redactor tests/test_redactor.cpp ${UTILS_SOURCES})
    target_include_directories(test_redactor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_redactor PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Test
    )
    add_test(NAME test_redactor COMMAND test_redactor)
endif()
