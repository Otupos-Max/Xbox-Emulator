add_library(xboxemu_memory
    MemoryManager.cpp
)

target_include_directories(xboxemu_memory
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)
