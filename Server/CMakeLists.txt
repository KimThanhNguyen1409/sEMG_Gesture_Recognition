cmake_minimum_required(VERSION 3.16)
project(semg_c_server C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(MOSQUITTO_ROOT "C:/Program Files/mosquitto" CACHE PATH "Path to Mosquitto installation")

add_executable(semg_server
    src/main.c
)

target_include_directories(semg_server PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/inc
    "${MOSQUITTO_ROOT}/devel"
)

if(WIN32)
    target_link_libraries(semg_server PRIVATE
        "${MOSQUITTO_ROOT}/mosquitto.dll"
        "${MOSQUITTO_ROOT}/mosquitto_common.dll"
        ws2_32
    )
else()
    target_link_libraries(semg_server PRIVATE
        mosquitto
        m
    )
endif()
