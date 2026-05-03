cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
	"${CMAKE_CURRENT_SOURCE_DIR}/../../libs/cookie_proto"
	"${CMAKE_CURRENT_SOURCE_DIR}/../../libs/cookie_sensors"
)

# Make Zephyr discover boards/cei/cookie_nrf_v200/ in this repo.
list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
list(APPEND DTS_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/../..")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cookie_sensor_node)

target_sources(app PRIVATE
	src/main.c
	src/thread_setup.c
	src/node_loop.c
	src/sed_loop.c
)
