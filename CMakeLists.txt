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

# X11 依赖（WindowSensor/InputSensor）
find_package(X11 QUIET)
if(X11_FOUND AND X11_Xrecord_LIB)
    message(STATUS "X11 Record extension found")
elseif(X11_FOUND)
    # libXtst 提供 XRecord 但 CMake 未检测到 X11_Xrecord_LIB
    find_library(XTST_LIB NAMES Xtst PATHS /usr/lib/x86_64-linux-gnu)
    if(XTST_LIB)
        set(X11_Xrecord_LIB ${XTST_LIB})
        message(STATUS "X11 Record extension found via libXtst: ${XTST_LIB}")
    else()
        message(WARNING "X11 Record extension NOT found - InputSensor will use fallback mode")
    endif()
else()
    message(WARNING "X11 Record extension NOT found - InputSensor will use fallback mode")
endif()

# QtGui/Widgets（ClipboardSensor 需要 QApplication）
find_package(Qt${QT_VERSION_MAJOR} COMPONENTS Gui Widgets QUIET)

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
    src/sensors/WindowSensor.cpp
    src/sensors/FileSensor.cpp
    src/sensors/ClipboardSensor.cpp
    src/sensors/InputSensor.cpp
    src/sensors/SystemSensor.cpp
    src/sensors/BrowserSensor.cpp
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

if(Qt${QT_VERSION_MAJOR}Gui_FOUND)
    target_link_libraries(deepin-environment-awareness PRIVATE Qt${QT_VERSION_MAJOR}::Gui)
endif()
if(Qt${QT_VERSION_MAJOR}Widgets_FOUND)
    target_link_libraries(deepin-environment-awareness PRIVATE Qt${QT_VERSION_MAJOR}::Widgets)
endif()
if(X11_FOUND)
    target_link_libraries(deepin-environment-awareness PRIVATE ${X11_LIBRARIES})
    if(X11_Xrecord_LIB)
        target_link_libraries(deepin-environment-awareness PRIVATE ${X11_Xrecord_LIB})
        target_compile_definitions(deepin-environment-awareness PRIVATE HAS_XRECORD=1)
    endif()
endif()

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

    # test_sensors: File/Window/Input/Browser (QTEST_APPLESS_MAIN)
    add_executable(test_sensors tests/test_sensors.cpp ${CORE_SOURCES} ${UTILS_SOURCES} ${SENSOR_SOURCES})
    target_include_directories(test_sensors PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_sensors PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    )
    if(X11_FOUND)
        target_link_libraries(test_sensors PRIVATE ${X11_LIBRARIES})
        if(X11_Xrecord_LIB)
            target_link_libraries(test_sensors PRIVATE ${X11_Xrecord_LIB})
            target_compile_definitions(test_sensors PRIVATE HAS_XRECORD=1)
        endif()
    endif()
    add_test(NAME test_sensors COMMAND test_sensors)

    # test_system_sensor: SystemSensor (需要定时器等待，独立测试)
    add_executable(test_system_sensor tests/test_system_sensor.cpp ${CORE_SOURCES} ${UTILS_SOURCES} ${SENSOR_SOURCES})
    target_include_directories(test_system_sensor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(test_system_sensor PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Sql
        Qt${QT_VERSION_MAJOR}::Gui
        Qt${QT_VERSION_MAJOR}::Widgets
        Qt${QT_VERSION_MAJOR}::Test
    )
    if(X11_FOUND)
        target_link_libraries(test_system_sensor PRIVATE ${X11_LIBRARIES})
        if(X11_Xrecord_LIB)
            target_link_libraries(test_system_sensor PRIVATE ${X11_Xrecord_LIB})
            target_compile_definitions(test_system_sensor PRIVATE HAS_XRECORD=1)
        endif()
    endif()
    add_test(NAME test_system_sensor COMMAND test_system_sensor)

    # test_clipboard_sensor: ClipboardSensor (需要 QApplication)
    if(Qt${QT_VERSION_MAJOR}Widgets_FOUND)
        add_executable(test_clipboard_sensor tests/test_clipboard_sensor.cpp ${CORE_SOURCES} ${UTILS_SOURCES} ${SENSOR_SOURCES})
        target_include_directories(test_clipboard_sensor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
        target_link_libraries(test_clipboard_sensor PRIVATE
            Qt${QT_VERSION_MAJOR}::Core
            Qt${QT_VERSION_MAJOR}::Gui
            Qt${QT_VERSION_MAJOR}::Widgets
            Qt${QT_VERSION_MAJOR}::Sql
            Qt${QT_VERSION_MAJOR}::Test
        )
        if(X11_FOUND)
            target_link_libraries(test_clipboard_sensor PRIVATE ${X11_LIBRARIES})
            if(X11_Xrecord_LIB)
                target_link_libraries(test_clipboard_sensor PRIVATE ${X11_Xrecord_LIB})
                target_compile_definitions(test_clipboard_sensor PRIVATE HAS_XRECORD=1)
            endif()
        endif()
        add_test(NAME test_clipboard_sensor COMMAND test_clipboard_sensor)
    endif()
