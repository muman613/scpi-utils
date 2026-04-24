add_executable(scpi-smoke
    src/main.cpp
)

target_link_libraries(scpi-smoke
    PRIVATE
        scpi::device
)

target_compile_features(scpi-smoke PRIVATE cxx_std_17)
target_compile_definitions(scpi-smoke PRIVATE SCPI_SMOKE_VERSION="${SCPI_SMOKE_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-smoke PRIVATE -Wall -Wextra -Wpedantic)
endif()
