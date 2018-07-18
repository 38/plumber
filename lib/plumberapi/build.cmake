if(NOT "${SHARED_LIBPLUMBER}" STREQUAL "yes")
	set(POST_CONF_SCRIPT "setup-static-library.cmake")
endif(NOT "${SHARED_LIBPLUMBER}" STREQUAL "yes")

set(TYPE shared-library)
set(LOCAL_CFLAGS "-fPIC")
set(LOCAL_LIBS)
set(INSTALL "yes")
install_includes("${SOURCE_PATH}/include" "include/plumberapi" "*.h")
install_plumber_headers("include/plumberapi")

configure_file("${SOURCE_PATH}/plumberapi-config.in" "${CMAKE_BINARY_DIR}/plumberapi-config" @ONLY)

install(FILES ${CMAKE_BINARY_DIR}/plumberapi-config DESTINATION bin PERMISSIONS WORLD_EXECUTE OWNER_EXECUTE GROUP_EXECUTE WORLD_READ OWNER_READ GROUP_READ OWNER_WRITE)

