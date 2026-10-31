# WebKitGTK compatibility shim for the Windows (MSVC) port of Evolution.
#
# Builds webkit2gtk-4.1.dll, which provides the subset of the WebKitGTK 4.1
# and JavaScriptCore-GLib APIs used by Evolution, to be implemented on top of
# Microsoft Edge WebView2. It installs the headers (<webkit2/webkit2.h>,
# <jsc/jsc.h>) and pkg-config files webkit2gtk-4.1.pc and
# javascriptcoregtk-4.1.pc, so that Evolution's build finds it like the real
# WebKitGTK. The single DLL provides both APIs.
#
# Built with win32/msvc/build-webkit-shim.bat (msvc-compat.cmake exports all
# symbols of the DLL).

cmake_minimum_required(VERSION 3.20)

project(webkit-shim
	VERSION 2.44.0
	LANGUAGES C CXX
)

set(API_VERSION 4.1)
set(LIBRARY_NAME webkit2gtk-${API_VERSION})

# Microsoft.Web.WebView2 NuGet package, build/native folder (headers and
# WebView2LoaderStatic.lib: no loader DLL to ship)
set(WEBVIEW2_SDK "" CACHE PATH "WebView2 SDK build/native directory")
if(NOT EXISTS "${WEBVIEW2_SDK}/include/WebView2.h")
	message(FATAL_ERROR "WebView2 SDK not found, set -DWEBVIEW2_SDK=<...>/build/native (see check-prereqs.ps1)")
endif()

find_package(PkgConfig REQUIRED)
pkg_check_modules(SHIM_DEPS REQUIRED
	gtk+-3.0
	libsoup-3.0
	json-glib-1.0
)

add_library(webkit2gtk SHARED
	src/webkit-shim-private.h
	src/jsc-shim.c
	src/webkit-enums.c
	src/webkit-misc.c
	src/webkit-navigation.c
	src/webkit-settings.c
	src/webkit-user-content.c
	src/webkit-web-context.c
	src/webkit-web-view.c
	src/webview2-host.h
	src/webview2-host.cpp
)

set_target_properties(webkit2gtk PROPERTIES
	OUTPUT_NAME ${LIBRARY_NAME}
	# no "lib" prefix: the import library must be webkit2gtk-4.1.lib for
	# pkg-config's -lwebkit2gtk-4.1
	PREFIX ""
	IMPORT_PREFIX ""
)

target_compile_definitions(webkit2gtk PRIVATE
	-DG_LOG_DOMAIN=\"WebKit\"
	-DGLIB_DISABLE_DEPRECATION_WARNINGS
)

target_include_directories(webkit2gtk PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/include
	${CMAKE_CURRENT_SOURCE_DIR}/src
	${SHIM_DEPS_INCLUDE_DIRS}
	${WEBVIEW2_SDK}/include
)

target_compile_options(webkit2gtk PRIVATE
	${SHIM_DEPS_CFLAGS_OTHER}
)

target_link_libraries(webkit2gtk PRIVATE
	${SHIM_DEPS_LDFLAGS}
	${WEBVIEW2_SDK}/x64/WebView2LoaderStatic.lib
	shlwapi
	ole32
)

install(TARGETS webkit2gtk
	RUNTIME DESTINATION bin
	LIBRARY DESTINATION lib
	ARCHIVE DESTINATION lib
)

install(DIRECTORY include/webkit2 include/jsc
	DESTINATION include/${LIBRARY_NAME}
	FILES_MATCHING PATTERN "*.h"
)

# pkg-config files, with forward slashes in the prefix
file(TO_CMAKE_PATH "${CMAKE_INSTALL_PREFIX}" PC_PREFIX)
configure_file(webkit2gtk.pc.in ${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_NAME}.pc @ONLY)
configure_file(javascriptcoregtk.pc.in ${CMAKE_CURRENT_BINARY_DIR}/javascriptcoregtk-${API_VERSION}.pc @ONLY)

install(FILES
	${CMAKE_CURRENT_BINARY_DIR}/${LIBRARY_NAME}.pc
	${CMAKE_CURRENT_BINARY_DIR}/javascriptcoregtk-${API_VERSION}.pc
	DESTINATION lib/pkgconfig
)
