execute_process(
	COMMAND ${PKG_CONFIG_EXECUTABLE} gio-2.0 --variable schemasdir
	OUTPUT_VARIABLE GLIB_SCHEMAS_DIR
	OUTPUT_STRIP_TRAILING_WHITESPACE
)
message("GIO Schemas will be installed to: " ${GLIB_SCHEMAS_DIR})

install(FILES com.nokia.mce.gschema.xml DESTINATION ${GLIB_SCHEMAS_DIR})

if(DEFINED GCONF_LIBRARIES)
	message("GConf support enabled")
	install(FILES devicelock.schemas security.schemas DESTINATION ${MCE_GCONF_DIR})
else()
	message("GConf support disabled")
endif(DEFINED GCONF_LIBRARIES)
