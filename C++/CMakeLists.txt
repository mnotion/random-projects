cmake_minimum_required(VERSION 3.21)

project(SimpleScreen LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)

qt_standard_project_setup()

qt_add_executable(SimpleScreen
    main.cpp
)

target_link_libraries(SimpleScreen PRIVATE Qt6::Widgets)

if(MSVC)
    target_compile_options(SimpleScreen PRIVATE /W4 /permissive- /utf-8)
else()
    target_compile_options(SimpleScreen PRIVATE -Wall -Wextra -Wpedantic)
endif()
