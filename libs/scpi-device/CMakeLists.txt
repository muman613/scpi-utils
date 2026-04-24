include(CMakePackageConfigHelpers)

add_library(scpi-device STATIC
    src/scpi_device.cpp
)

add_library(scpi::device ALIAS scpi-device)

set_target_properties(scpi-device PROPERTIES
    EXPORT_NAME device
    OUTPUT_NAME scpi-device
)

find_package(nlohmann_json REQUIRED)

target_include_directories(scpi-device
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/inc>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_definitions(scpi-device PRIVATE SCPI_DEVICE_VERSION="${SCPI_DEVICE_VERSION}")
target_compile_features(scpi-device PUBLIC cxx_std_17)
target_link_libraries(scpi-device PRIVATE nlohmann_json::nlohmann_json)

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(scpi-device PRIVATE -Wall -Wextra -Wpedantic)
endif()

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/scpi-deviceConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-deviceConfig.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scpi-device
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-deviceConfigVersion.cmake
    VERSION ${SCPI_DEVICE_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_file(
    ${PROJECT_SOURCE_DIR}/pkgconfig/scpi-device.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-device.pc
    @ONLY
)

install(TARGETS scpi-device
    EXPORT scpi-deviceTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/inc/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT scpi-deviceTargets
    FILE scpi-deviceTargets.cmake
    NAMESPACE scpi::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scpi-device
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-deviceConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-deviceConfigVersion.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scpi-device
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/scpi-device.pc
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
)
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
