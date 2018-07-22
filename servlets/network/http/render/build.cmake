list(APPEND LOCAL_LIBS pstd proto)
set(INSTALL yes)

find_package(PkgConfig)
pkg_check_modules(PC_ZLIB zlib)
if("${PC_ZLIB_FOUND}" STREQUAL "1")
	list(APPEND LOCAL_INCLUDE "${PC_ZLIB_INCLUDE_DIRS}")
       list(APPEND LOCAL_LIBS "${PC_ZLIB_LIBRARIES}")
	list(APPEND LOCAL_CFLAGS "-DHAS_ZLIB")
endif("${PC_ZLIB_FOUND}" STREQUAL "1")

#TODO: Add brotli support
