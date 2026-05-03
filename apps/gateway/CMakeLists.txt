cmake_minimum_required(VERSION 3.20.0)

list(APPEND ZEPHYR_EXTRA_MODULES
	"${CMAKE_CURRENT_SOURCE_DIR}/../../libs/cookie_proto"
)

list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../..")
list(APPEND DTS_ROOT   "${CMAKE_CURRENT_SOURCE_DIR}/../..")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(cookie_gateway)

target_sources(app PRIVATE
	src/main.c
	src/coap_server.c
	src/usb_print.c
)
