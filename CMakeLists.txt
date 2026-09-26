cmake_minimum_required(VERSION 3.21)
project(QSend VERSION 0.1.0 LANGUAGES CXX)
include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets Network)
if(MSVC)
    add_compile_options(/utf-8 /W4)
endif()

add_library(qsend_core STATIC
    src/core/models.h src/core/models.cpp
    src/core/requestengine.h src/core/requestengine.cpp
    src/core/workspacestore.h src/core/workspacestore.cpp)
target_include_directories(qsend_core PUBLIC src)
target_link_libraries(qsend_core PUBLIC Qt6::Network)

add_library(qsend_ui STATIC
    src/ui/keyvaluetable.h src/ui/keyvaluetable.cpp
    src/ui/jsonhighlighter.h src/ui/jsonhighlighter.cpp
    src/ui/mainwindow.h src/ui/mainwindow.cpp
    src/ui/theme.h)
target_link_libraries(qsend_ui PUBLIC qsend_core Qt6::Widgets)

qt_add_executable(QSend WIN32 src/main.cpp)
target_link_libraries(QSend PRIVATE qsend_ui)
target_compile_definitions(QSend PRIVATE QSEND_VERSION="${PROJECT_VERSION}")
set_target_properties(QSend PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.wangchaozhi.qsend"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}")
if(UNIX AND NOT APPLE)
    set_target_properties(QSend PROPERTIES INSTALL_RPATH "$ORIGIN/../lib")
endif()
install(TARGETS QSend BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
option(QSEND_DEPLOY_RUNTIME "Deploy Qt runtime dependencies at install time" OFF)
if(QSEND_DEPLOY_RUNTIME)
    if(Qt6_VERSION VERSION_LESS 6.8)
        message(FATAL_ERROR "Runtime packaging requires Qt 6.8 or newer")
    endif()
    qt_generate_deploy_app_script(TARGET QSend OUTPUT_SCRIPT deploy_script NO_TRANSLATIONS)
    install(SCRIPT ${deploy_script})
endif()

include(CTest)
if(BUILD_TESTING)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_executable(qsend_core_tests tests/core_tests.h tests/core_tests.cpp)
    target_link_libraries(qsend_core_tests PRIVATE qsend_core Qt6::Test)
    add_test(NAME core COMMAND qsend_core_tests)
    add_executable(qsend_ui_tests tests/ui_tests.cpp)
    target_link_libraries(qsend_ui_tests PRIVATE qsend_ui Qt6::Test)
    add_test(NAME ui COMMAND qsend_ui_tests -platform offscreen)
    set_tests_properties(core ui PROPERTIES TIMEOUT 60)
endif()
