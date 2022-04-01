set(MODULE_INCLUDE_DIRS .. ../utils ../include)

pkg_search_module(UPOWER upower-glib)
pkg_search_module(DSME dsme)
pkg_search_module(DEVLOCK libdevlock1)
find_package(X11)

add_library(alarm SHARED alarm.c)
target_link_libraries(alarm ${COMMON_LIBRARIES})
target_include_directories(alarm PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS alarm DESTINATION ${MCE_MODULE_DIR})

add_library(audiorouting SHARED audiorouting.c)
target_link_libraries(audiorouting ${COMMON_LIBRARIES})
target_include_directories(audiorouting PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS audiorouting DESTINATION ${MCE_MODULE_DIR})

add_library(battery-guard SHARED battery-guard.c)
target_link_libraries(battery-guard ${COMMON_LIBRARIES})
target_include_directories(battery-guard PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS battery-guard DESTINATION ${MCE_MODULE_DIR})

if(DEFINED UPOWER_LIBRARIES)
	add_library(battery-upower SHARED battery-upower.c)
	target_link_libraries(battery-upower ${COMMON_LIBRARIES} ${UPOWER_LIBRARIES})
	target_include_directories(battery-upower PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS} ${UPOWER_INCLUDE_DIRS})
	install(TARGETS battery-upower DESTINATION ${MCE_MODULE_DIR})
else()
	message("No upower found, upower support will not be built")
endif(DEFINED UPOWER_LIBRARIES)


add_library(button-backlight SHARED button-backlight.c)
target_link_libraries(button-backlight ${COMMON_LIBRARIES})
target_include_directories(button-backlight PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS button-backlight DESTINATION ${MCE_MODULE_DIR})

add_library(callstate SHARED callstate.c)
target_link_libraries(callstate ${COMMON_LIBRARIES})
target_include_directories(callstate PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS callstate DESTINATION ${MCE_MODULE_DIR})

add_library(camera SHARED camera.c)
target_link_libraries(camera ${COMMON_LIBRARIES})
target_include_directories(camera PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS camera DESTINATION ${MCE_MODULE_DIR})

add_library(display SHARED display.c)
target_link_libraries(display ${COMMON_LIBRARIES})
target_include_directories(display PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS display DESTINATION ${MCE_MODULE_DIR})

add_library(evdevvibrator SHARED evdevvibrator.c)
target_link_libraries(evdevvibrator ${COMMON_LIBRARIES})
target_include_directories(evdevvibrator PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS evdevvibrator DESTINATION ${MCE_MODULE_DIR})

add_library(filter-brightness-als-iio SHARED filter-brightness-als-iio.c)
target_link_libraries(filter-brightness-als-iio ${COMMON_LIBRARIES})
target_include_directories(filter-brightness-als-iio PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS filter-brightness-als-iio DESTINATION ${MCE_MODULE_DIR})

add_library(filter-brightness-simple SHARED filter-brightness-simple.c)
target_link_libraries(filter-brightness-simple ${COMMON_LIBRARIES})
target_include_directories(filter-brightness-simple PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS filter-brightness-simple DESTINATION ${MCE_MODULE_DIR})

add_library(iio-accelerometer SHARED iio-accelerometer.c)
target_link_libraries(iio-accelerometer ${COMMON_LIBRARIES})
target_include_directories(iio-accelerometer PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS iio-accelerometer DESTINATION ${MCE_MODULE_DIR})

add_library(iio-als SHARED iio-als.c)
target_link_libraries(iio-als ${COMMON_LIBRARIES})
target_include_directories(iio-als PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS iio-als DESTINATION ${MCE_MODULE_DIR})

add_library(iio-proximity SHARED iio-proximity.c)
target_link_libraries(iio-proximity ${COMMON_LIBRARIES})
target_include_directories(iio-proximity PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS iio-proximity DESTINATION ${MCE_MODULE_DIR})

add_library(inactivity SHARED inactivity.c)
target_link_libraries(inactivity ${COMMON_LIBRARIES})
target_include_directories(inactivity PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS inactivity DESTINATION ${MCE_MODULE_DIR})

add_library(inactivity-inhibit SHARED inactivity-inhibit.c)
target_link_libraries(inactivity-inhibit ${COMMON_LIBRARIES})
target_include_directories(inactivity-inhibit PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS inactivity-inhibit DESTINATION ${MCE_MODULE_DIR})

add_library(led-dbus SHARED led-dbus.c)
target_link_libraries(led-dbus ${COMMON_LIBRARIES})
target_include_directories(led-dbus PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS led-dbus DESTINATION ${MCE_MODULE_DIR})

add_library(led-lysti SHARED led-lysti.c)
target_link_libraries(led-lysti ${COMMON_LIBRARIES})
target_include_directories(led-lysti PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS led-lysti DESTINATION ${MCE_MODULE_DIR})

add_library(led-sw SHARED led-sw.c)
target_link_libraries(led-sw ${COMMON_LIBRARIES})
target_include_directories(led-sw PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS led-sw DESTINATION ${MCE_MODULE_DIR})

add_library(lock-generic SHARED lock-generic.c)
target_link_libraries(lock-generic ${COMMON_LIBRARIES})
target_include_directories(lock-generic PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS lock-generic DESTINATION ${MCE_MODULE_DIR})

add_library(power-generic SHARED power-generic.c)
target_link_libraries(power-generic ${COMMON_LIBRARIES})
target_include_directories(power-generic PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS power-generic DESTINATION ${MCE_MODULE_DIR})

if(DEFINED GCONF_LIBRARIES)
	add_library(rtconf-gconf SHARED rtconf-gconf.c)
	target_link_libraries(rtconf-gconf ${COMMON_LIBRARIES} ${GCONF_LIBRARIES})
	target_include_directories(rtconf-gconf PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS} ${GCONF_INCLUDE_DIRS})
	install(TARGETS rtconf-gconf DESTINATION ${MCE_MODULE_DIR})
endif(DEFINED GCONF_LIBRARIES)

add_library(rtconf-ini SHARED rtconf-ini.c)
target_link_libraries(rtconf-ini ${COMMON_LIBRARIES})
target_include_directories(rtconf-ini PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS rtconf-ini DESTINATION ${MCE_MODULE_DIR})

add_library(rtconf-gsettings SHARED rtconf-gsettings.c)
target_link_libraries(rtconf-gsettings ${COMMON_LIBRARIES})
target_include_directories(rtconf-gsettings PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS rtconf-gsettings DESTINATION ${MCE_MODULE_DIR})

if(DEFINED X11_LIBRARIES)
	add_library(x11-ctrl SHARED x11-ctrl.c)
	target_link_libraries(x11-ctrl 
		${COMMON_LIBRARIES} 
		${X11_LIBRARIES}
		-lXi
		${X11_dpms_LIBRARIES})
	target_include_directories(x11-ctrl PRIVATE 
		${COMMON_INCLUDE_DIRS} 
		${MODULE_INCLUDE_DIRS} 
		${X11_INCLUDE_DIRS} 
		${X11_dpms_INCLUDE_PATH}
		${X11_Xi_INCLUDE_PATH}
		${X11_dpms_INCLUDE_PATH})
	install(TARGETS x11-ctrl DESTINATION ${MCE_MODULE_DIR})
else()
	message("No xlib found, x11 support will not be built")
endif(DEFINED X11_LIBRARIES)

add_library(lock-tklock SHARED lock-tklock.c)
target_link_libraries(lock-tklock  ${COMMON_LIBRARIES})
target_include_directories(lock-tklock PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS lock-tklock DESTINATION ${MCE_MODULE_DIR})

if(DEFINED DEVLOCK_LIBRARIES)
	add_library(lock-devlock SHARED lock-devlock.c)
	target_link_libraries(lock-devlock 
		${COMMON_LIBRARIES} 
		${DEVLOCK_LIBRARIES})
	target_include_directories(lock-devlock PRIVATE 
		${COMMON_INCLUDE_DIRS} 
		${MODULE_INCLUDE_DIRS} 
		${DEVLOCK_INCLUDE_DIRS})
	install(TARGETS lock-devlock DESTINATION ${MCE_MODULE_DIR})
else()
	message("No devlock found, devlock support will not be built")
endif(DEFINED DEVLOCK_LIBRARIES)

if(DEFINED DSME_LIBRARIES)
	add_library(power-dsme SHARED power-dsme.c)
	target_link_libraries(power-dsme ${COMMON_LIBRARIES} ${DSME_LIBRARIES})
	target_include_directories(power-dsme PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS} ${DSME_INCLUDE_DIRS})
	install(TARGETS power-dsme DESTINATION ${MCE_MODULE_DIR})
else()
	message("No dsme support found, dsme support will not be built")
endif(DEFINED DSME_LIBRARIES)

add_library(state-dbus SHARED state-dbus.c)
target_link_libraries(state-dbus ${COMMON_LIBRARIES})
target_include_directories(state-dbus PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS state-dbus DESTINATION ${MCE_MODULE_DIR})

add_library(quirks-mapphone SHARED quirks-mapphone.c)
target_link_libraries(quirks-mapphone ${COMMON_LIBRARIES})
target_include_directories(quirks-mapphone PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS quirks-mapphone DESTINATION ${MCE_MODULE_DIR})

add_library(key-dbus SHARED key-dbus.c)
target_link_libraries(key-dbus ${COMMON_LIBRARIES})
target_include_directories(key-dbus PRIVATE ${COMMON_INCLUDE_DIRS} ${MODULE_INCLUDE_DIRS})
install(TARGETS key-dbus DESTINATION ${MCE_MODULE_DIR})
