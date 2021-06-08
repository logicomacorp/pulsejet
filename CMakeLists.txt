cmake_minimum_required(VERSION 3.13)
project(pulsejet)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -pedantic -Werror")
	set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -fno-strict-aliasing")
endif()

file(GLOB PULSEJET_HEADERS include/Pulsejet/*.hpp)
add_executable(
	pulsejet_demo
	demo/Demo.cpp
	demo/FastSinusoids.cpp
	demo/FastSinusoids.hpp
	${PULSEJET_HEADERS})
target_include_directories(pulsejet_demo PUBLIC include)
