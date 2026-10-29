cmake_minimum_required(VERSION 3.21)

project(PageConvert VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Pdf)

qt_standard_project_setup()

qt_add_executable(PageConvert WIN32
    main.cpp
)

target_link_libraries(PageConvert PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Pdf
)

if(MSVC)
    target_compile_options(PageConvert PRIVATE /W4 /permissive-)
else()
    target_compile_options(PageConvert PRIVATE -Wall -Wextra -Wpedantic)
endif()
