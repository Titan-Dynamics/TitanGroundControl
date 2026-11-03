# ============================================================================
# Titan USB Camera Module
# USB cameras and capture cards on Android through the app's own UVC driver,
# libusb and libuvc on a file descriptor from the USB host API, since Android's
# camera service lists them only where the camera HAL supports external
# cameras. The C++ types register in the app's QML module; elsewhere they find
# no cameras, as Qt's Camera covers USB cameras there.
# ============================================================================

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        TitanUsbCamera.cc
        TitanUsbCamera.h
        UsbCameraDevices.cc
        UsbCameraDevices.h
)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(NOT ANDROID)
    return()
endif()

target_sources(${CMAKE_PROJECT_NAME} PRIVATE UvcStream.cc UvcStream.h)

# ----------------------------------------------------------------------------
# libusb, built with its Android configuration
# https://github.com/libusb/libusb
# ----------------------------------------------------------------------------
CPMAddPackage(
    NAME libusb
    GITHUB_REPOSITORY libusb/libusb
    GIT_TAG v1.0.30
    DOWNLOAD_ONLY YES
)

qgc_require_cpm_added(libusb)

add_library(titan_libusb STATIC
    ${libusb_SOURCE_DIR}/libusb/core.c
    ${libusb_SOURCE_DIR}/libusb/descriptor.c
    ${libusb_SOURCE_DIR}/libusb/hotplug.c
    ${libusb_SOURCE_DIR}/libusb/io.c
    ${libusb_SOURCE_DIR}/libusb/strerror.c
    ${libusb_SOURCE_DIR}/libusb/sync.c
    ${libusb_SOURCE_DIR}/libusb/os/events_posix.c
    ${libusb_SOURCE_DIR}/libusb/os/linux_netlink.c
    ${libusb_SOURCE_DIR}/libusb/os/linux_usbfs.c
    ${libusb_SOURCE_DIR}/libusb/os/threads_posix.c
)
target_include_directories(titan_libusb
    PUBLIC
        ${libusb_SOURCE_DIR}/libusb
    PRIVATE
        ${libusb_SOURCE_DIR}/android   # config.h
        ${libusb_SOURCE_DIR}/libusb/os
)
set_target_properties(titan_libusb PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(titan_libusb PRIVATE log)
qgc_disable_dependency_warnings(titan_libusb)

# ----------------------------------------------------------------------------
# libuvc, on libusb, without its JPEG support (MJPEG is decoded with Qt)
# https://github.com/libuvc/libuvc
# ----------------------------------------------------------------------------
CPMAddPackage(
    NAME libuvc
    GITHUB_REPOSITORY libuvc/libuvc
    GIT_TAG v0.0.9
    DOWNLOAD_ONLY YES
)

qgc_require_cpm_added(libuvc)

set(libuvc_VERSION_MAJOR 0)
set(libuvc_VERSION_MINOR 0)
set(libuvc_VERSION_PATCH 9)
set(libuvc_VERSION 0.0.9)
configure_file(
    ${libuvc_SOURCE_DIR}/include/libuvc/libuvc_config.h.in
    ${CMAKE_CURRENT_BINARY_DIR}/libuvc_include/libuvc/libuvc_config.h
    @ONLY
)

add_library(titan_libuvc STATIC
    ${libuvc_SOURCE_DIR}/src/ctrl.c
    ${libuvc_SOURCE_DIR}/src/ctrl-gen.c
    ${libuvc_SOURCE_DIR}/src/device.c
    ${libuvc_SOURCE_DIR}/src/diag.c
    ${libuvc_SOURCE_DIR}/src/frame.c
    ${libuvc_SOURCE_DIR}/src/init.c
    ${libuvc_SOURCE_DIR}/src/misc.c
    ${libuvc_SOURCE_DIR}/src/stream.c
)
target_include_directories(titan_libuvc
    PUBLIC
        ${libuvc_SOURCE_DIR}/include
        ${CMAKE_CURRENT_BINARY_DIR}/libuvc_include
)
# Some Android USB controllers reject larger isochronous transfers (see LIBUVC_PACKETS_PER_TRANSFER_MAX in
# libuvc_internal.h: 8 packets work on most, some MediaTek controllers need 4)
target_compile_definitions(titan_libuvc PRIVATE LIBUVC_PACKETS_PER_TRANSFER_MAX=4)
set_target_properties(titan_libuvc PROPERTIES C_VISIBILITY_PRESET hidden)
target_link_libraries(titan_libuvc PUBLIC titan_libusb)
qgc_disable_dependency_warnings(titan_libuvc)

target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE titan_libuvc)
