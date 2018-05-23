/**
 * Copyright (C) 2018, Hao Hou
 **/

/**
 * @brief The field element parser
 * @file field/parse/include/parser.h
 **/

#ifndef __PARSER_H__
#define __PARSER_H__

#include <pstd.h>

/**
 * @brief Read next double value from the literal value represetation from a raw typed pipe
 * @param p_in The input pipe
 * @param buf  The buffer
 * @return status code
 **/
static inline int _parser_read_next_raw_literal_double(pstd_bio_t* p_in, double* buf)
{
	*buf = 0;

	char sbuf[128];
	unsigned size = 0;
	
	char ch;

	for(;size < sizeof(sbuf) - 1;)
	{
		int getc_rc = pstd_bio_getc(p_in, &ch);

		if(getc_rc == ERROR_CODE(int)) 
			ERROR_RETURN_LOG(int, "Cannot get the next char");

		if(getc_rc == 0)
		{
			int eof_rc = pstd_bio_eof(p_in);
			if(ERROR_CODE(int) == eof_rc)
				ERROR_RETURN_LOG(int, "Cannot check the EOF of the input pipe");

			if(eof_rc) 
				break;
			
			continue;
		}

		if(size == 0 && (ch == '\t' || ch == ' ' || ch == '\r' || ch == '\n'))
			continue;

		if(!(ch >= '0' && ch <= '9') && 
		  !(ch == '.' || ch == 'e' || ch == 'E' || ch == '+' || ch == '-' || ch == 'x'))
			break;
		sbuf[size ++] = ch;
	}



	if(size == sizeof(sbuf))
		ERROR_RETURN_LOG(int, "Number is too long");

	sbuf[size] = 0;

	char* endptr = NULL;
	*buf = strtod(sbuf, &endptr);

	if(NULL == endptr || *endptr != 0)
		ERROR_RETURN_LOG(int, "Invalid number");

	return 0;
}

/**
 * @brief Parse a literal representation of double from a string buffer
 * @param data The data section
 * @param end The end of the data section
 * @param buf The result buffer
 * @return status code
 **/
inline static int _parser_read_next_string_literal_double(char const** data, const  char* end, double* buf)
{
	while((**data == '\t' || **data == ' ' || **data == '\r' || **data == '\n') && (*data) < end)
		(*data) ++;

	if(*data >= end) 
		ERROR_RETURN_LOG(int, "No more data");

	const char* start = *data;
	char* endptr;

	*buf = strtod(start, &endptr);

	if(NULL == endptr)
		ERROR_RETURN_LOG(int, "Invalid number");

	*data += (uintptr_t)endptr - (uintptr_t)*data;

	return 0;
}

/**
 * @brief Parse a binary double value from a raw pipe
 * @param bio The bio object
 * @param buf The buffer
 * @return status code
 **/
inline static int _parser_read_next_raw_binary_double(pstd_bio_t* bio, double* buf)
{
	size_t bytes_to_read = sizeof(double);
	void* start = buf;

	while(bytes_to_read > 0)
	{
		size_t rc = pstd_bio_read(bio, start, bytes_to_read);
		if(ERROR_CODE(size_t) == rc)
			ERROR_RETURN_LOG(int, "Cannot read bytes from the raw pipe");

		bytes_to_read -= rc;

		start = ((char*)start) + rc;
	}

	return 0;
}

/**
 * @brief Parse a binary double from a string buffer
 * @param data The data buffer
 * @param end The end of the data
 * @buf The result buffer
 * @return status code
 **/
inline static int _parser_read_next_string_binary_double(char const** data, const char* end, double* buf)
{
	if(end < *data + sizeof(double))
		ERROR_RETURN_LOG(int, "No more data");

	union {
		const char*  s_data;
		const double* n_data;
	} view = {
		.s_data = *data
	};

	*buf = view.n_data[0];

	*data += sizeof(double);

	return 0;
}

/**
 * @brief The data source type
 **/
typedef enum {
	PARSER_SOURCE_TYPE_RAW_PIPE,    /*!< We need read the data from a raw pipe */
	PARSER_SOURCE_TYPE_STR_BUF      /*!< We need read the data from a string buffer */
} parser_source_type_t;

/**
 * @brief The data represtation method
 **/
typedef enum {
	PARSER_REPR_LITERAL,       /*!< The literal representation */
	PARSER_REPR_BINARY         /*!< The binary representation */
} parser_repr_type_t;

/**
 * @brief The type of the value
 **/
typedef enum {
	PARSER_VALUE_TYPE_DOUBLE  /*!< The element should be a double */
} parser_value_type_t;

/**
 * @brief The parser request
 **/
typedef struct {
	union {
		pstd_bio_t*      raw;        /*!< The BIO for the raw pipe */
		struct {
			char const** begin;      /*!< The begining of the string buffer */
			char const*  end;        /*!< The end of the string buffer */
		}                s_buf;      /*!< The string buffer */
	}                    source;     /*!< The data source */
	parser_source_type_t src_type:2; /*!< The data source type */
	parser_repr_type_t   repr:2;     /*!< The data represtation type */
	parser_value_type_t  type:2;     /*!< The actual data type */
} parser_request_t;

/**
 * @brief The result buffer
 **/
typedef union {
	double*          d;          /*!< The double percision floating point number */
	void*            generic;    /*!< The generic pointer */
} parser_result_buf_t;

/**
 * @brief Parse the next value
 * @param req The parser request
 * @return status code
 **/
static inline int parser_next_value(parser_request_t req, parser_result_buf_t buf)
{
	if(req.type == PARSER_VALUE_TYPE_DOUBLE)
	{
		if(req.src_type == PARSER_SOURCE_TYPE_RAW_PIPE)
		{
			if(req.repr == PARSER_REPR_LITERAL)
				return _parser_read_next_raw_literal_double(req.source.raw, buf.d);
			else if(req.repr == PARSER_REPR_BINARY)
				return _parser_read_next_raw_binary_double(req.source.raw, buf.d);
			else ERROR_RETURN_LOG(int, "Invalid representation type");
		}
		else if(req.src_type == PARSER_SOURCE_TYPE_STR_BUF)
		{
			if(req.repr == PARSER_REPR_LITERAL)
				return _parser_read_next_string_literal_double(req.source.s_buf.begin, req.source.s_buf.end, buf.d); 
			else if(req.repr == PARSER_REPR_BINARY)
				return _parser_read_next_string_binary_double(req.source.s_buf.begin, req.source.s_buf.end, buf.d);
			else ERROR_RETURN_LOG(int, "Invalid representation type");
		}
		else ERROR_RETURN_LOG(int, "Invalid data source type");
	}
	else ERROR_RETURN_LOG(int, "Invalid element type");
}

#endif /* __PARSER_H__ */
