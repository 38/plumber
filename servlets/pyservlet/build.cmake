find_package(PythonLibs)
list(APPEND LOCAL_INCLUDE ${PYTHON_INCLUDE_DIR})
list(APPEND LOCAL_LIBS ${PYTHON_LIBRARIES})
set(INSTALL yes)
install_includes("${SOURCE_PATH}/PyServlet" "lib/plumber/python/PyServlet" "*.py")
set(NAMESPACE language_binding)
