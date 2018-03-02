/**
 * Copyright (C) 2018, Hao Hou
 **/
#ifndef __HTTP_H__
#define __HTTP_H__

/**
 * @brief The data structure used to keep tracking the HTTP response
 **/
typedef struct {
	uint32_t           response_completed:1; /*!< If this repsonse is compeleted */
	uint32_t           size_determined:1;    /*!< Indicates if the response has determined size or figured out it's a chunked response */
	uint32_t           chunked:1;           /*!< Indicates this response is chunked */
	uint32_t           body_started:1;       /*!< If the body has started */
	uint8_t            parts;                /*!< Which of the parts we are parsing */
	uint8_t            parser_state;         /*!< The internal parser state */
	uint8_t            remaining_key_len;    /*!< The length of the remaining key */
	const char*        remaining_key;        /*!< The remaining key that still not matched till last blocks of read data */
	size_t             chunk_remaining;     /*!< The remaining bytes in previous chunck */
} http_response_t;

/**
 * @brief Feed next len bytes from data buffer to the response object
 * @param res The response object
 * @param data The data length
 * @param len The length of the data section
 * @return if the response is still valid, or error code
 **/
int http_response_parse(http_response_t* res, const char* data, size_t len);

/**
 * @brief Check if the response has completed
 * @param res The response object
 * @return check result
 **/
static inline int http_response_complete(const http_response_t* res)
{
	return res->response_completed;
}

#endif
