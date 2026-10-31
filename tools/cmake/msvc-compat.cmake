# MSVC compatibility layer injected into every project of the Windows port
# with -DCMAKE_PROJECT_INCLUDE=<this file>.
#
# The GNOME projects pass pkg-config's raw *_LDFLAGS ("-L/dir -lfoo") to
# target_link_libraries() and to CMAKE_REQUIRED_LIBRARIES of configure checks.
# With MSVC, CMake hands them verbatim to link.exe, which ignores them
# (LNK4044). This layer rewrites them into link.exe syntax:
# "-L<dir>" -> "-LIBPATH:<dir>", "-l<name>" -> "<name>.lib".

if(NOT MSVC OR COMMAND _msvc_compat_translate_link_items)
	return()
endif()

# Configure-time test programs (try_compile/try_run) default to the Debug
# configuration, i.e. the debug CRT, whose assertions open modal dialogs
# (e.g. strftime() with a POSIX-only format) and block the configure step.
set(CMAKE_TRY_COMPILE_CONFIGURATION Release)

# The GNOME libraries rely on the ELF default of exporting every non-static
# function from shared libraries; MSVC exports nothing without
# __declspec(dllexport), so no import library would be produced. Exported
# *data* symbols still need explicit declspecs on the consumer side.
set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)

# Plugins (MODULE libraries) export their entry points (e.g.
# camel_provider_module_init, e_plugin_lib_enable) without annotations, but
# WINDOWS_EXPORT_ALL_SYMBOLS only applies to SHARED libraries. Build them as
# SHARED: same DLL, all symbols exported, plus an unused import library.
# They keep their Unix "lib" prefix: the loaders look them up by that name
# (e.g. Camel providers: libcamelimapx.urls -> libcamelimapx.dll).
set(CMAKE_SHARED_MODULE_PREFIX "lib")

function(add_library _name)
	if(ARGC GREATER 1 AND "${ARGV1}" STREQUAL "MODULE")
		set(_args ${ARGN})
		list(REMOVE_AT _args 0)
		_add_library(${_name} SHARED ${_args})
		set_target_properties(${_name} PROPERTIES PREFIX "lib")
	else()
		_add_library(${_name} ${ARGN})
	endif()
endfunction()

# ICU >= 75 C++ headers need C++17; MSVC defaults to C++14.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Sources and headers are UTF-8; without this MSVC reads them in the ANSI
# code page (CP1252 here), which mangles non-ASCII string literals.
add_compile_options(/utf-8)

# Standard-conforming preprocessor. The traditional one passes __VA_ARGS__
# forwarded to another macro as a single argument: with
#   #define d(...) camel_imapx_debug (debug, '?', __VA_ARGS__)
# the format and its arguments shift ("%s" received '?', crash in printf).
# GLib-style code forwards __VA_ARGS__ in many places, silently wrong
# without this switch.
add_compile_options(/Zc:preprocessor)

# Minimal POSIX headers missing from MSVC (unistd.h, dirent.h, regex.h,
# sys/time.h, sys/param.h), see win32/msvc/compat/include.
get_filename_component(_msvc_compat_include "${CMAKE_CURRENT_LIST_DIR}/../compat/include" ABSOLUTE)
include_directories(BEFORE SYSTEM "${_msvc_compat_include}")
list(APPEND CMAKE_REQUIRED_INCLUDES "${_msvc_compat_include}")
# POSIX types (mode_t, pid_t, ssize_t, S_I*) and Winsock ordering, see the header
add_compile_options("/FI${_msvc_compat_include}/msvc-prelude.h")
# regex.h maps to PCRE2's POSIX wrapper
link_libraries(pcre2-posix)
# Winsock (struct timeval in sys/time.h, sockets everywhere)
link_libraries(ws2_32)
unset(_msvc_compat_include)

# GLib's code generators are Python scripts without extension on Windows,
# which cmd.exe cannot run. find_program() keeps an already set cache
# variable, so preset them as "<python> <script>" (a two-element list that
# add_custom_command() expands into the interpreter and its argument).
find_program(_msvc_compat_python NAMES python3 python REQUIRED)
foreach(_msvc_tool IN ITEMS GLIB_MKENUMS:glib-mkenums GLIB_GENMARSHAL:glib-genmarshal GDBUS_CODEGEN:gdbus-codegen)
	string(REPLACE ":" ";" _msvc_tool "${_msvc_tool}")
	list(GET _msvc_tool 0 _msvc_var)
	list(GET _msvc_tool 1 _msvc_script)
	find_file(_msvc_script_path ${_msvc_script} PATHS ${CMAKE_PREFIX_PATH} PATH_SUFFIXES bin NO_DEFAULT_PATH)
	if(_msvc_script_path)
		set(${_msvc_var} "${_msvc_compat_python};${_msvc_script_path}" CACHE STRING "${_msvc_script} run through Python")
	endif()
	unset(_msvc_script_path CACHE)
endforeach()
unset(_msvc_tool)
unset(_msvc_var)
unset(_msvc_script)

# intltool (translations merged into .desktop/.xml files) is a set of Perl
# scripts, taken from MSYS2 (pacman -S intltool) and run with its perl.
if(DEFINED ENV{MSYS2_ROOT})
	file(TO_CMAKE_PATH "$ENV{MSYS2_ROOT}/usr/bin" _msvc_msys_bin)
	if(EXISTS "${_msvc_msys_bin}/perl.exe")
		foreach(_msvc_tool IN ITEMS update extract merge)
			string(TOUPPER "INTLTOOL_${_msvc_tool}" _msvc_var)
			if(EXISTS "${_msvc_msys_bin}/intltool-${_msvc_tool}")
				set(${_msvc_var} "${_msvc_msys_bin}/perl.exe;${_msvc_msys_bin}/intltool-${_msvc_tool}" CACHE STRING "intltool-${_msvc_tool} run through MSYS2 perl")
			endif()
		endforeach()
	endif()
	unset(_msvc_msys_bin)
	unset(_msvc_tool)
	unset(_msvc_var)
endif()

function(_msvc_compat_translate_link_items _out)
	set(_result)
	foreach(_item IN LISTS ARGN)
		# a whole LDFLAGS string passed as one argument
		if(_item MATCHES "^-[lL].* ")
			separate_arguments(_split WINDOWS_COMMAND "${_item}")
		else()
			set(_split "${_item}")
		endif()

		foreach(_arg IN LISTS _split)
			if(_arg MATCHES "^-(pthread|rdynamic|lm|ldl|Wl,.*)$" OR _arg STREQUAL "m")
				# GNU-only linker flags, nothing to do with MSVC (libm and
				# libdl are part of the CRT / kernel32)
			elseif(_arg MATCHES "^-LIBPATH:")
				# already translated
				list(APPEND _result "${_arg}")
			elseif(_arg MATCHES "^-L(.+)$")
				file(TO_CMAKE_PATH "${CMAKE_MATCH_1}" _dir)
				list(APPEND _result "-LIBPATH:${_dir}")
			elseif(_arg MATCHES "^-l(.+)$")
				list(APPEND _result "${CMAKE_MATCH_1}.lib")
			else()
				list(APPEND _result "${_arg}")
			endif()
		endforeach()
	endforeach()
	set(${_out} "${_result}" PARENT_SCOPE)
endfunction()

function(target_link_libraries _target)
	_msvc_compat_translate_link_items(_items ${ARGN})
	_target_link_libraries(${_target} ${_items})
endfunction()

# Configure checks: translate CMAKE_REQUIRED_LIBRARIES for the duration of the
# check. The modules use include_guard(GLOBAL), so including them here first
# makes the projects' own include() calls no-ops and keeps these wrappers.
# Wrappers are macros (the checks set cache/result variables in the caller's
# scope) with explicit parameters, so that source code containing ';' is
# forwarded as a single quoted argument.

include(CheckCSourceCompiles)
include(CheckCSourceRuns)
include(CheckCXXSourceCompiles)
include(CheckFunctionExists)
include(CheckLibraryExists)
include(CheckSymbolExists)
include(CheckIncludeFile)

macro(_msvc_compat_push_required_libraries)
	set(_msvc_compat_saved_required_libraries "${CMAKE_REQUIRED_LIBRARIES}")
	_msvc_compat_translate_link_items(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES})
endmacro()

macro(_msvc_compat_pop_required_libraries)
	set(CMAKE_REQUIRED_LIBRARIES "${_msvc_compat_saved_required_libraries}")
	unset(_msvc_compat_saved_required_libraries)
endmacro()

macro(CHECK_C_SOURCE_COMPILES _msvc_src _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_C_SOURCE_COMPILES("${_msvc_src}" ${_msvc_var} ${ARGN})
	_msvc_compat_pop_required_libraries()
endmacro()

macro(CHECK_C_SOURCE_RUNS _msvc_src _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_C_SOURCE_RUNS("${_msvc_src}" ${_msvc_var} ${ARGN})
	_msvc_compat_pop_required_libraries()
endmacro()

macro(CHECK_CXX_SOURCE_COMPILES _msvc_src _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_CXX_SOURCE_COMPILES("${_msvc_src}" ${_msvc_var} ${ARGN})
	_msvc_compat_pop_required_libraries()
endmacro()

macro(CHECK_FUNCTION_EXISTS _msvc_func _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_FUNCTION_EXISTS("${_msvc_func}" ${_msvc_var})
	_msvc_compat_pop_required_libraries()
endmacro()

macro(CHECK_LIBRARY_EXISTS _msvc_lib _msvc_func _msvc_location _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_LIBRARY_EXISTS("${_msvc_lib}" "${_msvc_func}" "${_msvc_location}" ${_msvc_var})
	_msvc_compat_pop_required_libraries()
endmacro()

macro(CHECK_SYMBOL_EXISTS _msvc_symbol _msvc_files _msvc_var)
	_msvc_compat_push_required_libraries()
	_CHECK_SYMBOL_EXISTS("${_msvc_symbol}" "${_msvc_files}" ${_msvc_var})
	_msvc_compat_pop_required_libraries()
endmacro()
