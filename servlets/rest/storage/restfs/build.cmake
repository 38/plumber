if("${build_jsonschema}" STREQUAL "yes")
	find_package(PkgConfig)
	pkg_check_modules(PC_UUID uuid)
	if("${PC_UUID_FOUND}" STREQUAL "1")
		set(LOCAL_INCLUDE ${PC_UUID_INCLUDE_DIRS})
	else("${PC_UUID_FOUND}" STREQUAL "1")
		message("Missing libuuid, restfs servlet disabled")
		set(build_restfs "no")
	endif("${PC_UUID_FOUND}" STREQUAL "1")
	pkg_check_modules(PC_JSON_C json-c)
	if("${PC_JSON_C_FOUND}" STREQUAL "1")
		set(LOCAL_INCLUDE ${PC_JSON_C_INCLUDE_DIRS})
		set(LOCAL_LIBS pstd proto ${PC_JSON_C_LIBRARIES})
	else("${PC_JSON_C_FOUND}" STREQUAL "1")
		message("Missing libuuid, restfs servlet disabled")
		set(build_restfs "no")
	endif("${PC_JSON_C_FOUND}" STREQUAL "1")
	set(LOCAL_LIBS pstd proto ${PC_UUID_LIBRARIES} ${PC_JSON_C_LIBRARIES})
	set(INSTALL yes)
else("${build_jsonschema}" STREQUAL "yes")
	set(build_restfs "no")
endif("${build_jsonschema}" STREQUAL "yes")
