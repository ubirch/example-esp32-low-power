set(COMPONENT_SRCS
        main.c
        sensor.c
        util.c
        )

set(COMPONENT_ADD_INCLUDEDIRS "${CMAKE_CURRENT_LIST_DIR}")

set(COMPONENT_REQUIRES
        arduino-esp32
        ubirch-esp32-storage
        ubirch-mbed-msgpack
        ubirch-protocol
        ubirch-esp32-networking
        ubirch-esp32-console
        ubirch-esp32-api-http
        ubirch-esp32-key-storage
        ubirch-esp32-ota
        )

register_component()
