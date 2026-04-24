find_package(sdbus-c++ REQUIRED CONFIG)
find_package(sdbus-c++-tools REQUIRED CONFIG)

set(SCPI_SERVICE_DBUS_XML ${PROJECT_SOURCE_DIR}/dbus/scpi-service.xml)
set(SCPI_SERVICE_PROXY ${CMAKE_CURRENT_BINARY_DIR}/scpi-service-proxy.h)

add_custom_command(
    OUTPUT
        ${SCPI_SERVICE_PROXY}
    COMMAND
        $<TARGET_FILE:SDBusCpp::sdbus-c++-xml2cpp>
        ${SCPI_SERVICE_DBUS_XML}
        --proxy=${SCPI_SERVICE_PROXY}
    DEPENDS
        ${SCPI_SERVICE_DBUS_XML}
        SDBusCpp::sdbus-c++-xml2cpp
    VERBATIM
)

add_executable(scpi-util
    src/main.cpp
    ${SCPI_SERVICE_PROXY}
)

target_link_libraries(scpi-util
    PRIVATE
        scpi::device
        SDBusCpp::sdbus-c++
)

target_include_directories(scpi-util PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(scpi-util PRIVATE SCPI_UTIL_VERSION="${SCPI_UTIL_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-util PRIVATE -Wall -Wextra -Wpedantic)
endif()
