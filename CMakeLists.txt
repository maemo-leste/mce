cmake_minimum_required(VERSION 3.1)

project(mce LANGUAGES C VERSION 1.9)

set(MCE_CONF_DIR /etc/mce)
set(MCE_CONF_OVR_DIR mce.ini.d)
set(MCE_MODULE_DIR lib/mce/modules)
set(MCE_RUN_DIR /var/run/mce)
set(MCE_VAR_DIR /var/lib/mce)
set(MCE_GCONF_DIR /etc/gconf/schemas/)
set(DBUS_CONF_DIR /etc/dbus-1/system.d)

add_definitions(-D_GNU_SOURCE)
add_definitions(-DMCE_VAR_DIR=${MCE_VAR_DIR})
add_definitions(-DMCE_RUN_DIR=${MCE_RUN_DIR})
add_definitions(-DMCE_CONF_DIR=${MCE_CONF_DIR})
add_definitions(-DMCE_CONF_OVERRIDE_DIR=${MCE_CONF_OVR_DIR})
add_definitions(-DMCE_CONF_FILE=mce.ini)

find_package(PkgConfig REQUIRED)
pkg_search_module(GLIB REQUIRED glib-2.0)
pkg_search_module(GIO REQUIRED gio-2.0)
pkg_search_module(GMODULE REQUIRED gmodule-2.0)
pkg_search_module(DBUS REQUIRED dbus-1)
pkg_search_module(GDBUS REQUIRED dbus-glib-1)
pkg_search_module(SYSTEMUI osso-systemui)
pkg_search_module(CONIC conic)
pkg_search_module(GCONF gconf-2.0)

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

if(DEFINED GCONF_LIBRARIES)
	message("GConf support enabled")
	install(FILES schemas/devicelock.schemas schemas/security.schemas DESTINATION ${MCE_GCONF_DIR})
else()
	message("GConf support disabled")
endif(DEFINED GCONF_LIBRARIES)

configure_file(mce.pc.in "${CMAKE_CURRENT_BINARY_DIR}/mce.pc"  @ONLY)

install(FILES "${CMAKE_CURRENT_BINARY_DIR}/mce.pc" DESTINATION "${CMAKE_INSTALL_LIBDIR}/pkgconfig")
install(DIRECTORY config/mce.ini.d DESTINATION ${MCE_CONF_DIR})
install(FILES config/mce.ini config/rtconf.ini  DESTINATION ${MCE_CONF_DIR})
install(FILES config/mode DESTINATION ${MCE_VAR_DIR})
install(FILES config/mce.conf DESTINATION ${DBUS_CONF_DIR})
install(DIRECTORY src/include/ DESTINATION include FILES_MATCHING PATTERN "*.h")

if(DEFINED SYSTEMUI_LIBRARIES)
	message("SystemUI support enabled")
	install(FILES config/10-maemo.ini DESTINATION ${MCE_CONF_DIR}/${MCE_CONF_OVR_DIR})
	message("SystemUI support enabled")
else()
	message("SystemUI support disabled")
endif(DEFINED SYSTEMUI_LIBRARIES)
