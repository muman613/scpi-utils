find_package(sdbus-c++ REQUIRED CONFIG)
find_package(sdbus-c++-tools REQUIRED CONFIG)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBUDEV REQUIRED libudev)
endif()

set(SCPI_SERVICE_DBUS_XML ${PROJECT_SOURCE_DIR}/dbus/scpi-service.xml)
set(SCPI_SERVICE_ADAPTOR ${CMAKE_CURRENT_BINARY_DIR}/scpi-service-adaptor.h)
set(SCPI_SERVICE_PROXY ${CMAKE_CURRENT_BINARY_DIR}/scpi-service-proxy.h)

add_custom_command(
    OUTPUT
        ${SCPI_SERVICE_ADAPTOR}
        ${SCPI_SERVICE_PROXY}
    COMMAND
        $<TARGET_FILE:SDBusCpp::sdbus-c++-xml2cpp>
        ${SCPI_SERVICE_DBUS_XML}
        --adaptor=${SCPI_SERVICE_ADAPTOR}
        --proxy=${SCPI_SERVICE_PROXY}
    DEPENDS
        ${SCPI_SERVICE_DBUS_XML}
        SDBusCpp::sdbus-c++-xml2cpp
    VERBATIM
)

add_executable(scpi-service
    src/main.cpp
    ${SCPI_SERVICE_ADAPTOR}
    ${SCPI_SERVICE_PROXY}
)

target_link_libraries(scpi-service
    PRIVATE
        scpi::device
        SDBusCpp::sdbus-c++
)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_include_directories(scpi-service PRIVATE ${LIBUDEV_INCLUDE_DIRS})
    target_link_libraries(scpi-service PRIVATE ${LIBUDEV_LIBRARIES})
    target_compile_definitions(scpi-service PRIVATE SCPI_SERVICE_HAS_UDEV=1)
endif()

target_compile_features(scpi-service PRIVATE cxx_std_17)
target_include_directories(scpi-service PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_compile_definitions(scpi-service PRIVATE SCPI_SERVICE_VERSION="${SCPI_SERVICE_VERSION}")

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-service PRIVATE -Wall -Wextra -Wpedantic)
endif()
