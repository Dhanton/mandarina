cmake_minimum_required(VERSION 3.9.1)
project(MandarinaTest)

if("${PROJECT_SOURCE_DIR}" STREQUAL "${PROJECT_BINARY_DIR}")
    message(FATAL_ERROR "In-source builds are not allowed, use a separate build directory.")
endif()

set(PATH_SFML_include     "")
set(PATH_SFML_lib         "")
set(PATH_rapidjson        "")
set(PATH_sockets_include  "")
set(PATH_sockets_lib      "")

if("${PATH_SFML_include}" STREQUAL "")
    message(FATAL_ERROR "Missing SFML include directory")
endif()

if("${PATH_SFML_lib}" STREQUAL "")
    message(FATAL_ERROR "Missing SFML lib directory")
endif()

if("${PATH_rapidjson}" STREQUAL "")
    message(FATAL_ERROR "Missing rapidjson include directory")
endif()

if("${PATH_sockets_include}" STREQUAL "")
    message(FATAL_ERROR "Missing GameNetworkingSockets include directory")
endif()

if("${PATH_sockets_lib}" STREQUAL "")
    message(FATAL_ERROR "Missing GameNetworkingSockets lib directory")
endif()

set(CMAKE_CXX_STANDARD 11)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -DMANDARINA_DEBUG")

#required by GameNetworkingSockets (for UNIX systems)
if (UNIX AND NOT APPLE)
    add_definitions(-DPOSIX)

    if (NOT APPLE)
        add_definitions(-DLINUX)
    else()
        add_definitions(-DOSX)
    endif()
endif ()

include_directories("../include")

set(SRC_DIR "../src")
file(GLOB SRC_FILES "${SRC_DIR}/*.cpp" "${INCLUDE_DIR}/*.inl" "${SRC_DIR}/entities/*.cpp" "${SRC_DIR}/abilities/*.cpp" "${SRC_DIR}/buffs/*.cpp" "${SRC_DIR}/game_modes/*.cpp")

#remove the other main.cpp
get_filename_component(main_full_path "../src/main.cpp" ABSOLUTE)
list(REMOVE_ITEM SRC_FILES "${CMAKE_SOURCE_DIR}/../src/main.cpp")

#SFML
include_directories(${PATH_SFML_include})
set(SFML_LIBS_D debug libsfml-audio-d.so debug libsfml-graphics-d.so debug libsfml-network-d.so debug libsfml-window-d.so debug libsfml-system-d.so)
set(SFML_LIBS_R optimized libsfml-audio.so optimized libsfml-graphics.so optimized libsfml-network.so optimized libsfml-window.so optimized libsfml-system.so)
link_directories(${PATH_SFML_lib})

#rapidjson
include_directories(${PATH_rapidjson})

#GameNetworkingSockets
include_directories(${PATH_sockets_include})
set(NETWORK_LIBS libGameNetworkingSockets.so)
link_directories(${PATH_sockets_lib})

add_executable(mandarina_tests_main ${SRC_FILES} "tests_main.cpp")
target_link_libraries(mandarina_tests_main stdc++fs ${SFML_LIBS_D} ${SFML_LIBS_R} libGL.so ${NETWORK_LIBS} libssl.so libcrypto.so pthread)

add_executable(mandarina_test_packet ${SRC_FILES} "test_packet.cpp")
target_link_libraries(mandarina_test_packet stdc++fs ${SFML_LIBS_D} ${SFML_LIBS_R} libGL.so ${NETWORK_LIBS} libssl.so libcrypto.so pthread)
