cmake_minimum_required(VERSION 3.21)
project(textdichter VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets PrintSupport OPTIONAL_COMPONENTS LinguistTools Test)
qt_standard_project_setup(I18N_TRANSLATED_LANGUAGES ru)

find_package(PkgConfig REQUIRED)
pkg_check_modules(CMARK REQUIRED IMPORTED_TARGET libcmark)

# Everything but main(), shared by the application and the tests.
qt_add_library(textdichter_core STATIC
    src/mainwindow.cpp src/mainwindow.h
    src/markdown.cpp src/markdown.h
    src/textfile.cpp src/textfile.h
    src/views.cpp src/views.h
)
target_include_directories(textdichter_core PUBLIC src)
target_compile_definitions(textdichter_core PUBLIC TEXTDICHTER_VERSION="${PROJECT_VERSION}")
target_link_libraries(textdichter_core PUBLIC Qt6::Widgets Qt6::PrintSupport PkgConfig::CMARK)

qt_add_executable(textdichter src/main.cpp)
target_link_libraries(textdichter PRIVATE textdichter_core)

if(Qt6LinguistTools_FOUND)
    qt_add_translations(textdichter SOURCE_TARGETS textdichter textdichter_core TS_FILE_DIR translations)
else()
    message(WARNING "Qt6 LinguistTools not found: building without translations")
endif()

include(CTest)
if(BUILD_TESTING AND Qt6Test_FOUND)
    qt_add_executable(textdichter_tests tests/tests.cpp)
    target_link_libraries(textdichter_tests PRIVATE textdichter_core Qt6::Test)
    target_compile_definitions(textdichter_tests PRIVATE TESTS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests")
    add_test(NAME textdichter_tests COMMAND textdichter_tests)
    set_tests_properties(textdichter_tests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

install(TARGETS textdichter)
