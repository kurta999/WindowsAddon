add_library(windowshelper_protocol STATIC
    bitfield/8byte.c
    bitfield/bitarray.c
    bitfield/bitfield.c
    bitfield/read.c
    bitfield/write.c
    isotp/isotp.c
    sha256/sha256.c
)

target_include_directories(windowshelper_protocol PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_definitions(windowshelper_protocol PRIVATE _CRT_SECURE_NO_WARNINGS)
windowshelper_configure_own_target(windowshelper_protocol)
