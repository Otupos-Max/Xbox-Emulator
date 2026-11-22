add_library(xboxemu_cpu
    XenonCPU.cpp
    PowerPC/PPCDecoder.cpp
    PowerPC/PPCInterpreter.cpp
)

target_include_directories(xboxemu_cpu
    PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/../..
)

target_link_libraries(xboxemu_cpu
    PUBLIC xboxemu_memory
)