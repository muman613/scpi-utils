add_library(scpi-device STATIC
    src/scpi_device.cpp
)

add_library(scpi::device ALIAS scpi-device)

find_package(nlohmann_json REQUIRED)

target_include_directories(scpi-device
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/inc
)

target_compile_features(scpi-device PUBLIC cxx_std_17)
target_link_libraries(scpi-device PRIVATE nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-device PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(scpi-device-test
        tests/scpi_device_test.cpp
    )

    target_link_libraries(scpi-device-test PRIVATE scpi::device)
    target_compile_features(scpi-device-test PRIVATE cxx_std_17)

    if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
        target_compile_options(scpi-device-test PRIVATE -Wall -Wextra -Wpedantic)
    endif()

    if(UNIX AND NOT APPLE)
        target_link_libraries(scpi-device-test PRIVATE util)
    endif()

    add_test(NAME scpi-device-test COMMAND scpi-device-test)
endif()
