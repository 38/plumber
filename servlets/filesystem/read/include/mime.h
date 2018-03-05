/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The MIME type mapping
 * @file filesystem/read/include/mime.h
 **/
#ifndef __MIME_H__
#define __MIME_H__

/**
 * @brief The information about this mime type
 **/
typedef struct {
	const char*    mime_type;       /*!< The mime type string */
	uint32_t       can_compress:1;  /*!< If this mime type can compress */
} mime_map_info_t;

/**
 * @brief The MIME type mapping object
 **/
typedef struct _mime_map_t mime_map_t;

/**
 * @brief Create new MIME type map
 * @param sepc_file The mime type spec file
 * @param compress_list The list seperated wildcard list for the types that can compress
 * @return The newly create mime type
 **/
mime_map_t* mime_map_new(const char* spec_file, const char* compress_list);

/**
 * @brief Dispose a used MIME type mapping file
 * @param map The map to dispose
 * @return status code
 **/
int mime_map_free(mime_map_t* map);

/**
 * @brief Query a extension name
 * @param ext_name The extension name
 * @param length The length of the extension name
 * @param res The result buffer
 * @return status code
 **/
int mime_map_query(const char* ext_name, size_t length, mime_map_info_t* res);

#endif
