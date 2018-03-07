/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The MIME type map
 * @file readfile/include/mime.h
 **/
#ifndef __MIME_H__
#define __MIME_H__

/**
 * @brief The MIME type map
 **/
typedef struct _mime_map_t mime_map_t;

/**
 * @brief The MIME type information
 **/
typedef struct {
	const char*       mime_type;       /*!< The MIME type string */
	uint32_t          compressable:1;  /*!< If the type is compressable */
} mime_map_info_t;

/**
 * @brief Load the mime type map 
 * @param map_file The path to the map file
 * @param compress The compressable MIME type list
 * @param default_mime_type The default MIME type
 * @return newly created MIME map
 **/
mime_map_t* mime_map_new(const char* map_file, const char* compress, const char* default_mime_type);

/**
 * @brief Query the extension name
 * @param map The map to query
 * @param extname The extension name
 * @param buf The result buffer
 * @return status code
 **/
int mime_map_query(const mime_map_t* map, const char* extname, mime_map_info_t* buf);

/**
 * @brief Dispose the MIME map 
 * @param map The map to dispose
 * @return status code
 **/
int mime_map_free(mime_map_t* map);

#endif 
