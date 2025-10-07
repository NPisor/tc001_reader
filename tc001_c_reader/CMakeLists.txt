cmake_minimum_required(VERSION 3.20)
project(tc001 C)

# ---- Options ----
option(TC001_BUILD_SHARED   "Build shared (DLL/.so) library" ON)
option(TC001_BUILD_STATIC   "Build static library"            ON)
option(TC001_BUILD_EXAMPLES "Build examples/reader"           ON)

# Enforce C11
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Position-independent code (for static on *nix/Android)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# ---- libusb paths ----
# Desktop: place libusb under external/libusb with include/libusb.h and lib/<lib>
# Android: preferably static libusb in external/libusb-android with:
#   include/libusb-1.0/libusb.h  and lib/libusb-1.0.a
set(LIBUSB_ROOT "${CMAKE_SOURCE_DIR}/external/libusb" CACHE PATH "Root of libusb (desktop)")
set(LIBUSB_ANDROID_ROOT "${CMAKE_SOURCE_DIR}/external/libusb-android" CACHE PATH "Root of libusb (Android)")

set(LIBUSB_INCLUDE_DIR "")
set(LIBUSB_LIBRARY "")

if (ANDROID)
  # Try static libusb first
  if (EXISTS "${LIBUSB_ANDROID_ROOT}/include/libusb-1.0/libusb.h")
    set(LIBUSB_INCLUDE_DIR "${LIBUSB_ANDROID_ROOT}/include/libusb-1.0")
  elseif (EXISTS "${LIBUSB_ANDROID_ROOT}/include/libusb.h")
    set(LIBUSB_INCLUDE_DIR "${LIBUSB_ANDROID_ROOT}/include")
  endif()

  find_library(LIBUSB_LIBRARY
    NAMES usb-1.0 libusb-1.0 libusb-1.0.a
    PATHS "${LIBUSB_ANDROID_ROOT}/lib" "${LIBUSB_ANDROID_ROOT}/libs" NO_DEFAULT_PATH)

  if (NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "Android: libusb not found. Set LIBUSB_ANDROID_ROOT to a libusb install (include/libusb-1.0 + lib/libusb-1.0.a).")
  endif()
else()
  # Desktop
  if (EXISTS "${LIBUSB_ROOT}/include/libusb.h")
    set(LIBUSB_INCLUDE_DIR "${LIBUSB_ROOT}/include")
  elseif (EXISTS "${LIBUSB_ROOT}/include/libusb-1.0/libusb.h")
    set(LIBUSB_INCLUDE_DIR "${LIBUSB_ROOT}/include/libusb-1.0")
  endif()

  if (MSVC)
    find_library(LIBUSB_LIBRARY NAMES libusb-1.0 libusb-1.0.lib
      PATHS "${LIBUSB_ROOT}/lib" NO_DEFAULT_PATH)
  else()
    find_library(LIBUSB_LIBRARY NAMES usb-1.0 libusb-1.0 libusb-1.0.dll.a
      PATHS "${LIBUSB_ROOT}/lib" /usr/lib /usr/local/lib /opt/homebrew/lib)
  endif()

  if (NOT LIBUSB_INCLUDE_DIR OR NOT LIBUSB_LIBRARY)
    message(FATAL_ERROR "Desktop: libusb not found. Set LIBUSB_ROOT correctly (include + lib).")
  endif()
endif()

# ---- Sources ----
set(TC001_PUBLIC_HEADERS
  core/include/tc001.h
)

set(TC001_COMMON
  core/src/tc001.c
)

if (WIN32)
  set(TC001_PLATFORM_SRC core/src/platform_win.c)
else()
  set(TC001_PLATFORM_SRC core/src/platform_posix.c)
endif()

set(TC001_SOURCES ${TC001_COMMON} ${TC001_PLATFORM_SRC})

# ---- Warnings / defs ----
if (MSVC)
  add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
  add_compile_options(/W3)
else()
  add_compile_options(-Wall -Wextra -Wno-unused-parameter)
endif()

# ---- Shared lib ----
if (TC001_BUILD_SHARED)
  add_library(tc001 SHARED ${TC001_SOURCES} ${TC001_PUBLIC_HEADERS})
  target_include_directories(tc001 PUBLIC core/include ${LIBUSB_INCLUDE_DIR})
  target_link_libraries(tc001 PUBLIC ${LIBUSB_LIBRARY})
  target_compile_definitions(tc001 PRIVATE TC001_BUILD_DLL)
  if (ANDROID)
    target_link_libraries(tc001 PRIVATE log android)
  endif()
  set_target_properties(tc001 PROPERTIES OUTPUT_NAME "tc001" VERSION 1.0 SOVERSION 1)
endif()

# ---- Static lib ----
if (TC001_BUILD_STATIC)
  add_library(tc001_static STATIC ${TC001_SOURCES} ${TC001_PUBLIC_HEADERS})
  target_include_directories(tc001_static PUBLIC core/include ${LIBUSB_INCLUDE_DIR})
  target_link_libraries(tc001_static PUBLIC ${LIBUSB_LIBRARY})
  target_compile_definitions(tc001_static PUBLIC TC001_STATIC)
  if (ANDROID)
    target_link_libraries(tc001_static PRIVATE log android)
  endif()
  set_target_properties(tc001_static PROPERTIES OUTPUT_NAME "tc001")
endif()

# ---- Example ----
if (TC001_BUILD_EXAMPLES)
  add_executable(reader examples/reader.c)
  target_include_directories(reader PRIVATE core/include)
  if (TARGET tc001)
    target_link_libraries(reader PRIVATE tc001)
  else()
    target_link_libraries(reader PRIVATE tc001_static)
  endif()
endif()

# ---- Windows: copy libusb-1.0.dll next to targets ----
if (WIN32 AND EXISTS "${LIBUSB_ROOT}/bin/libusb-1.0.dll")
  foreach(tgt IN ITEMS reader tc001)
    if (TARGET ${tgt})
      add_custom_command(TARGET ${tgt} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
          "${LIBUSB_ROOT}/bin/libusb-1.0.dll"
          "$<TARGET_FILE_DIR:${tgt}>")
    endif()
  endforeach()
endif()

# ---- Install (optional, nice for reuse) ----
include(GNUInstallDirs)
if (TARGET tc001)
  install(TARGETS tc001
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
if (TARGET tc001_static)
  install(TARGETS tc001_static
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()
install(FILES ${TC001_PUBLIC_HEADERS} DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
