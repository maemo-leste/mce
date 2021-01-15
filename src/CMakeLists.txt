set(MCE_SRC_FILES 	mce.c 
					utils/datapipe.c
					utils/event-input.c 
					utils/event-input-utils.c 
					utils/event-switches.c 
					utils/mce-conf.c 
					utils/mce-dbus.c 
					utils/mce-io.c 
					utils/mce-lib.c 
					utils/mce-log.c 
					utils/mce-modules.c 
					utils/mce-rtconf.c 
					utils/modetransition.c 
					utils/powerkey.c )

if(DEFINED CONIC_LIBRARIES)
set(MCE_SRC_FILES 	${MCE_SRC_FILES} utils/connectivity.c)
endif(DEFINED CONIC_LIBRARIES)

add_executable(mce ${MCE_SRC_FILES})
target_link_libraries(mce ${COMMON_LIBRARIES})
target_include_directories(mce PRIVATE ${COMMON_INCLUDE_DIRS} . utils include)
install(TARGETS mce)

add_executable(devlock-blocker devlock-blocker.c utils/mce-log.c)
target_link_libraries(devlock-blocker ${COMMON_LIBRARIES})
target_include_directories(devlock-blocker PRIVATE ${COMMON_INCLUDE_DIRS} . utils include)
install(TARGETS devlock-blocker)

