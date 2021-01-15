cmake_minimum_required(VERSION 3.1)

project(mce LANGUAGES C)

set(MCE_CONF_DIR /etc/mce)
set(MCE_MODULE_DIR lib/mce/modules)
set(MCE_RUN_DIR /var/run/mce)
set(MCE_VAR_DIR /var/lib/mce)
set(DBUS_CONF_DIR /etc/dbus-1/system.d)

add_definitions(-D_GNU_SOURCE)
add_definitions(-DMCE_VAR_DIR=${MCE_VAR_DIR})
add_definitions(-DMCE_RUN_DIR=${MCE_RUN_DIR})
add_definitions(-DMCE_CONF_DIR=${MCE_CONF_DIR})
add_definitions(-DMCE_CONF_OVERRIDE_DIR=mce.ini.d)
add_definitions(-DMCE_CONF_FILE=mce.ini)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GIO REQUIRED gio-2.0)
pkg_search_module(GMODULE REQUIRED gmodule-2.0)
pkg_search_module(DBUS REQUIRED dbus-1)
pkg_search_module(GDBUS REQUIRED dbus-glib-1)
pkg_search_module(SYSTEMUI osso-systemui)
pkg_search_module(CONIC conic)

add_compile_options( 
	-Wextra
	-Wall
	-Wpointer-arith 
	-Wundef
	-Wcast-align
	-Wshadow
	-Wbad-function-cast
	-Wwrite-strings
	-Wsign-compare
	-Wmissing-noreturn
	-Wnested-externs
	-Wchar-subscripts
	-Wmissing-prototypes
	-Wformat-security
	-Wformat=2
	-Wformat-nonliteral
	-Winit-self
	-Wswitch-default
	-Wstrict-prototypes
	-Wold-style-definition
	-Wmissing-declarations
	-Wmissing-include-dirs
	-Wstrict-aliasing=2
	-Wunsafe-loop-optimizations
	-Winvalid-pch
	-Waddress
	-Wvolatile-register-var
	-Wstrict-overflow=5
	-Wunreachable-code
	-Wstack-protector )

if(NOT DEFINED SYSTEMUI_LIBRARIES)
	set(SYSTEMUI_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/dbus-names)
	message("Not using system installed systemui dbus names")
endif(NOT DEFINED SYSTEMUI_LIBRARIES)

if(DEFINED CONIC_LIBRARIES)
	add_definitions(-DENABLE_CONIC_SUPPORT)
	message("Conic support enabled")
else()
	message("Conic support disabled")
endif(DEFINED CONIC_LIBRARIES)

set(COMMON_INCLUDE_DIRS 
	${GLIB_INCLUDE_DIRS} 
	${GIO_INCLUDE_DIRS} 
	${GMODULE_INCLUDE_DIRS} 
	${DBUS_INCLUDE_DIRS} 
	${GDBUS_INCLUDE_DIRS}
	${CONIC_INCLUDE_DIRS}
	${SYSTEMUI_INCLUDE_DIRS})

set(COMMON_LIBRARIES 
	${DBUS_LIBRARIES} 
	${GLIB_LIBRARIES}
	${GIO_LIBRARIES} 
	${GMODULE_LIBRARIES}
	${CONIC_LIBRARIES}
	${GDBUS_LIBRARIES})


add_subdirectory(src)
add_subdirectory(src/modules)

install(DIRECTORY config/mce.ini.d DESTINATION /etc/mce)
install(FILES config/mce.ini config/rtconf.ini  DESTINATION ${MCE_CONF_DIR})
install(FILES config/mode DESTINATION ${MCE_VAR_DIR})
install(FILES config/mce.conf DESTINATION ${DBUS_CONF_DIR})
