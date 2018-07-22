find_package(PythonLibs)
set(build_language_pyservlet "no")
if("${PYTHONLIBS_FOUND}" STREQUAL "TRUE")
	aux_source_directory(${SOURCE_PATH}/scope LOCAL_SOURCE)
       list(APPEND LOCAL_INCLUDE ${PYTHON_INCLUDE_DIR})
       list(APPEND LOCAL_LIBS ${PYTHON_LIBRARIES})
	list(APPEND LOCAL_LIBS pstd proto)
	install_includes("${SOURCE_PATH}/PyServlet" "lib/plumber/python/PyServlet" "*.py")
       set(build_language_pyservlet "yes")
       set(INSTALL yes)
elseif("${PYTHONLIBS_FOUND}" STREQUAL "TRUE")
       message("No python development files found, disabling language.pyservlet")
endif("${PYTHONLIBS_FOUND}" STREQUAL "TRUE")
