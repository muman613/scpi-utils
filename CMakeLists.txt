cmake_minimum_required(VERSION 3.16)

project(scpi-utils VERSION 0.1.1 LANGUAGES CXX)

set(SCPI_DEVICE_VERSION 0.1.1)
set(SCPI_UTIL_VERSION 0.1.1)
set(SCPI_SERVICE_VERSION 0.1.0)
set(SCPI_SMOKE_VERSION 0.1.1)

include(GNUInstallDirs)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(CTest)

add_subdirectory(libs/scpi-device)
add_subdirectory(apps/scpi-util)
add_subdirectory(apps/scpi-service)
add_subdirectory(apps/scpi-smoke)
