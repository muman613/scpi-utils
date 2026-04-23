add_executable(scpi-util
    src/main.cpp
)

target_link_libraries(scpi-util PRIVATE scpi::device)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-util PRIVATE -Wall -Wextra -Wpedantic)
endif()
