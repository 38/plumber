#misc targets

find_package(Doxygen)
add_custom_target(docs
	COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/doc/
	COMMAND ${DOXYGEN_EXECUTABLE} Doxyfile
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
	COMMENT "Generating API documentation with Doxygen" VERBATIM
)

add_custom_target(distclean
	COMMAND make clean
	COMMAND rm -rf CMakeFiles CMakeCache.txt Testing doc/doxygen cmake_install.cmake CTestTestfile.cmake Makefile tags bin config.h cmake_uninstall.cmake install_manifest.txt servlet.mk
	COMMAND rm -rf tools/*/package_config.h lib/*/package_config.h install-prototype.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

add_custom_target(tags
	COMMAND ctags -R ${CMAKE_CURRENT_SOURCE_DIR}/include/ 
	                 ${CMAKE_CURRENT_SOURCE_DIR}/src/ 
	                 ${CMAKE_CURRENT_SOURCE_DIR}/tools/ 
	                 ${CMAKE_CURRENT_SOURCE_DIR}/lib/ 
					 ${CMAKE_CURRENT_SOURCE_DIR}/servlets/
					 ${CMAKE_CURRENT_BINARY_DIR}/ 
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)


configure_file("${CMAKE_CURRENT_SOURCE_DIR}/build/cmake_uninstall.cmake.in"
	           "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
			   IMMEDIATE @ONLY)

add_custom_target(uninstall
	COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

add_custom_target(show-flags
	COMMAND echo "Compiler       = ${CMAKE_C_COMPILER}" && 
	        echo "Log Level      = ${LOG}" &&
			echo "Optimaization  = ${OPTLEVEL}" &&
			echo "Compiler Flags = ${CFLAGS}" &&
			echo "Linker Flags   = ${LIBS}" &&
			echo "Shared Libplumber = ${SHARED_LIBPLUMBER}" &&
			echo "Config CommandLine: $ENV{CFLAGS} CC=${CMAKE_C_COMPILER} CXX=${CMAKE_CXX_COMPILER} cmake -DCMAKE_INSTALL_PREFIX=${CMAKE_INSTALL_PREFIX} -DPLUMBER_V8_PREFIX=${PLUMBER_V8_PREFIX} -DLOG=${LOG} -DOPTLEVEL=${OPTLEVEL} ${package_status} ${option_status} ${CMAKE_SOURCE_DIR}" )	

add_custom_target(book
	COMMAND mkdir -p ${CMAKE_CURRENT_BINARY_DIR}/bin/book
	COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/doc/book/build.sh
	WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/bin/book
)

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/misc/install-prototype.sh.in"
	           "${CMAKE_CURRENT_BINARY_DIR}/install-prototype.sh"
			   @ONLY)

