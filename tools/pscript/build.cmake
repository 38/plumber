find_library(READLINE_LIBRARY readline)
set(TYPE binary)
set(LOCAL_LIBS plumber dl pss ${READLINE_LIBRARY})
set(INSTALL yes)
install_includes("${SOURCE_PATH}/pss" "lib/plumber/pss" "*.pss")
