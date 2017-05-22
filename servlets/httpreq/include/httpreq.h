/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief the header file for the HTTP request parser servlet
 * @file servlets/httpreq/include/httpreq.h
 **/
#ifndef __HTTPREQ_H__
#define __HTTPREQ_H__

/**
 * @brief the type used to describe a HTTP verb
 * @note the reason why we need to use this enum is we do not want to
 *       do string parse twice. <br/>
 *       See <a href="https://www.w3.org/Protocols/rfc2616/rfc2616-sec9.html>RFC 2616 Section 9</a> for details
 *
 **/
typedef enum {
	HTTPREQ_VERB_ERROR   = -1,   /*!< the invalid HTTP verb */
	HTTPREQ_VERB_OPTIONS = 0,    /*!< OPTIONS */
	HTTPREQ_VERB_GET     = 1,    /*!< GET */
	HTTPREQ_VERB_HEAD    = 2,    /*!< HEAD */
	HTTPREQ_VERB_POST    = 3,    /*!< POST */
	HTTPREQ_VERB_PUT     = 4,    /*!< PUT */
	HTTPREQ_VERB_DELETE  = 5,    /*!< DELETE */
	HTTPREQ_VERB_TRACE   = 6,    /*!< TRACE */
	HTTPREQ_VERB_CONNECT = 7,    /*!< CONNECT */
	HTTPREQ_VERB_COUNT
} httpreq_verb_t;


#endif /* __HTTP_REQ_H__ */
