if(CONFIG_COOKIE_PROTO)
	zephyr_library_named(cookie_proto)
	zephyr_library_sources(
		src/frame_json.c
		src/coap_client.c
	)
	zephyr_include_directories(include)
endif()
