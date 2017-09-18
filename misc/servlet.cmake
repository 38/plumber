macro(FindPlumber outvar)
	unset(${outvar}_PREFIX CACHE)
	find_path(${outvar}_PREFIX NAMES "lib/plumber/servlet.mk" HINTS "${PLUMBER_HINT}")
	if("${${outvar}_PREFIX}" STREQUAL "${outvar}_PREFIX-NOTFOUND")
		message(FATAL_ERROR "Plumber installation not found")
	else("${${outvar}_PREFIX}" STREQUAL "${outvar}_PREFIX-NOTFOUND")
		message("Found libplumber at ${${outvar}_PREFIX}")
	endif("${${outvar}_PREFIX}" STREQUAL "${outvar}_PREFIX-NOTFOUND")
endmacro(FindPlumber outvar)

set(SERVLET_OUTPUT_DIR "bin")

add_custom_target(servlet-clean
	COMMAND ${CMAKE_MAKE_PROGRAM} clean
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})

macro(AddServlet servlet files)

	FindPlumber(PLUMBER)

	if("" STREQUAL "${CMAKE_BUILD_TYPE}")
		set(CMAKE_BUILD_TYPE DEBUG)
	endif("" STREQUAL "${CMAKE_BUILD_TYPE}")

	set(SERVLET_NAME ${servlet})

	set(MAKEFILE ${CMAKE_CURRENT_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/${SERVLET_NAME}/build.mk)

	set(PLUMBER_INCLUDES)
	set(PLUMBER_LIBS -L${PLUMBER_PREFIX})
	foreach(lib ${${servlet}_PLUMBER_LIBS})
		set(PLUMBER_INCLUDES "${PLUMBER_INCLUDES} -I${PLUMBER_PREFIX}/include/${lib}")
		set(PLUMBER_LIBS "${PLUMBER_LIBS} -l${lib}")
	endforeach(lib ${${servlet}_PLUMBER_LIBS})

	file(WRITE ${MAKEFILE} 
		 "LINKER=${CMAKE_C_COMPILER}\n"
		 "LDFLAGS=${LDFLAGS} ${CMAKE_LD_FLAGS_${CMAKE_BUILD_TYPE}} ${PLUMBER_LIBS} ${${servlet}_LDFLAGS}\n"
		 "CFLAGS=${CFLAGS} ${CMAKE_C_FLAGS_${CMAKE_BUILD_TYPE}} -I${CMAKE_CURRENT_SOURCE_DIR}/include ${PLUMBER_INCLUDES} ${${servlet}_CFLAGS}\n"
		 "OUTPUT=${CMAKE_CURRENT_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/${SERVLET_NAME}\n"
		 "SRC=$(subst ;, ,${SERVLET_SOURCE})\n"
		 "TARGET=${SERVLET_NAME}")

	file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/${SERVLET_NAME})

	add_custom_target(${servlet} ALL 
		COMMAND make -f ${PLUMBER_PREFIX}/lib/plumber/servlet.mk
		BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/lib${SERVLET_NAME}.so
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/${SERVLET_NAME}
		SOURCES ${SERVLET_SOURCE})

	add_custom_target(servlet-clean-${servlet} 
		COMMAND make -f ${PLUMBER_PREFIX}/lib/plumber/servlet.mk clean
		WORKING_DIRECTORY ${CMAKE_BINARY_DIR}/${SERVLET_OUTPUT_DIR}/${SERVLET_NAME})

	add_dependencies(servlet-clean servlet-clean-${servlet})
		
endmacro(AddServlet files)


