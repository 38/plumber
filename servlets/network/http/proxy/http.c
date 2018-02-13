/**
 * Copyright (C) 2018, Hao Hou
 **/
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>

#include <pservlet.h>

#include <error.h>
#include <fallthrough.h>

#include <http.h>

enum {
	_NONE,/*!< Not a particular thing we are paring */
	_CL,  /*!< Content-Length */
	_TE,  /*!< Transfer-Encoding */
	_CH   /*!< Chunck Header */
};


/**
 * @brief The key for the Content-Length field
 **/
const char _content_length_key[] = "\r\ncontent-length:";

/**
 * @brief The key for the transfer encoding field
 **/
const char _transfer_encodeing_key[] = "\r\ntransfer-encoding:";

const char _chunked[] = "chunked";

const char _body_start[] = "\r\n\r\n";

/**
 * @brief Compare the string ignore the case
 * @param a first string
 * @param b second string
 * @param n The size to compare
 * @return If two string matches
 **/
static inline int _match(const char* a, const char* b, size_t n)
{
	size_t i;
	for(i = 0; i < n; i ++)
	{
		int ch_a = a[i];
		int ch_b = b[i];

		if(ch_a >= 'A' && ch_a <= 'Z')
			ch_a += 32;
		if(ch_b >= 'A' && ch_b <= 'Z')
			ch_b += 32;

		if(ch_a != ch_b) return 0;
	}

	return 1;
}

static inline size_t _parse_transfer_encoding(http_response_t* res, const char* data, size_t len)
{
	size_t i = 0;
	if(res->parser_state == 0)
	{
		for(i = 0; i < len && (data[i] == '\t' || data[i] == ' '); i ++);
		if(i < len && data[i] != '\t' && data[i] != ' ')
			res->parser_state = 1;
	}

	while(res->parser_state > 0 && res->parser_state < sizeof(_chunked))
	{
		if(i < len && data[i] == _chunked[res->parser_state - 1])
			res->parser_state ++, i ++;
		else if(i < len)
			res->parser_state = sizeof(_chunked);
		else return i;
	}

	if(res->parser_state == sizeof(_chunked) && i < len)
	{
		if(data[i] == '\r')
			res->parser_state ++, i++;
		else
			i ++;
	}

	if(res->parser_state == sizeof(_chunked) + 1 && i < len)
	{
		if(i < len && data[i] == '\n')
			res->parts = _NONE, i ++, res->size_determined = 1, res->chunked = 1, res->parts = _CH, res->parser_state = 0;
		else 
			return ERROR_CODE(size_t);
	}

	return i;
}

static inline size_t _parse_chunk_size(http_response_t* res, const char* data, size_t len)
{
	size_t i = 0;
	if(res->parser_state == 0)
	{
		if(i < len && data[i] == '\r')
			res->parser_state = 1, i ++;
		else if(i < len)
			res->parser_state = 2;
	}

	if(res->parser_state == 1)
	{
		if(i < len && data[i] == '\n')
			res->parser_state = 2, i ++;
		else if(i < len)
			return ERROR_CODE(size_t);
	}
	if(res->parser_state == 2)
	{
		for(; i < len; i ++)
		{
			char ch = data[i];
			if(ch >= '0' && ch <= '9')
				res->chunk_remaining = res->chunk_remaining * 16 + (uint32_t)(ch - '0');
			else if(ch >= 'a' && ch <= 'f')
				res->chunk_remaining = res->chunk_remaining * 16 + (uint32_t)(ch - 'a') + 10;
			else if(ch >= 'A' && ch <= 'f')
				res->chunk_remaining = res->chunk_remaining * 16 + (uint32_t)(ch - 'A') + 10;
			else
			{
				res->parser_state = 3;
				break;
			}
		}

	}

	if(res->parser_state == 3)
	{
		for(; i < len && (data[i] == '\t' || data[i] == ' '); i ++);
		if(i < len && data[i] != '\t' && data[i] != ' ')
			res->parser_state = 4;
	}

	if(res->parser_state == 4)
	{
		if(i < len && data[i] == '\r')
			res->parser_state = 5, i ++;
		else if(i < len)
			return ERROR_CODE(size_t);
	}

	if(res->parser_state == 5)
	{
		if(i < len && data[i] == '\n')
			res->parts = _NONE, i++; 
		else if(i < len)
			return ERROR_CODE(size_t);
	}

	return i;
}

static inline size_t _parse_content_length(http_response_t* res, const char* data, size_t len)
{
	size_t i = 0;
	if(res->parser_state == 0)
	{
		for(i = 0; i < len && (data[i] == '\t' || data[i] == ' '); i ++);
		if(i < len && data[i] != '\t' && data[i] != ' ')
			res->parser_state = 1;
	}

	if(res->parser_state == 1)
	{
		for(; i < len && (data[i] >= '0' && data[i] <= '9'); i ++)
			res->chunk_remaining = res->chunk_remaining * 10 + (unsigned)(data[i] - '0');
		if(i < len && (data[i] < '0' || data[i] > '9'))
			res->parser_state = 2;
	}

	if(res->parser_state == 2)
	{
		if(i < len && data[i] == '\r')
			res->parser_state = 3, i ++;
		else if(i < len)
			return ERROR_CODE(size_t);
	}

	if(res->parser_state == 3)
	{
		if(i < len && data[i] == '\n')
			res->parts = _NONE, i ++, res->size_determined = 1;
		else if(i < len)
			return ERROR_CODE(size_t);
	}

	return i;
}

static inline size_t _detect_header(http_response_t* res, const char* data, size_t len)
{
	size_t ret = 0;

	if(res->remaining_key != NULL)
	{
		uint8_t to_compare = res->remaining_key_len;
		if(to_compare > len)
			to_compare = (uint8_t)len;

		if(_match(data, res->remaining_key, to_compare))
		{
			res->remaining_key += to_compare;
			res->remaining_key_len = (uint8_t)(res->remaining_key_len - to_compare);
		}

		if(res->remaining_key_len == 0)
		{
			if(res->remaining_key == _content_length_key + sizeof(_content_length_key) - 1)
				res->parts = _CL, res->parser_state = 0;
			else if(res->remaining_key == _body_start + sizeof(_body_start) - 1)
				res->parts = _TE, res->parser_state = 0;
			else
				res->body_started = 1;
			res->remaining_key = NULL;
			return to_compare;
		}
	}

	for(ret = 0; ret < len; ret ++)
	{
		uint64_t u64_data = 0;
		
		switch(len - ret)
		{
			case 7: u64_data |= ((uint64_t)(uint8_t)data[ret + 6]) << 56; FALLTHROUGH();
			case 6: u64_data |= ((uint64_t)(uint8_t)data[ret + 5]) << 48; FALLTHROUGH();
			case 5: u64_data |= ((uint64_t)(uint8_t)data[ret + 4]) << 32; FALLTHROUGH();
			case 4: u64_data |= ((uint64_t)(uint8_t)data[ret + 3]) << 24; FALLTHROUGH();
			case 3: u64_data |= ((uint64_t)(uint8_t)data[ret + 2]) << 16; FALLTHROUGH();
			case 2: u64_data |= ((uint64_t)(uint8_t)data[ret + 1]) << 8;  FALLTHROUGH();
			case 1: u64_data |= ((uint64_t)(uint8_t)data[ret + 0]) ; break;
			default: u64_data = *(const uint64_t*)(data + ret);
		}

		u64_data |= 0x2020202020202020ull;

		const char* key = NULL;
		size_t key_len = 0;
		uint8_t parts = 0;

		if(!res->size_determined)
		{
			if(u64_data == (0x2020202020202020ull | *(const uint64_t*)_content_length_key))
				key = _content_length_key, key_len = sizeof(_content_length_key) - 1, parts = _CL;

			if(u64_data == (0x2020202020202020ull | *(const uint64_t*)_transfer_encodeing_key))
				key = _transfer_encodeing_key, key_len = sizeof(_transfer_encodeing_key) - 1, parts = _TE;
		}

		if((u64_data & 0xffffffff) == 0x2a2d2a2du)
			key = _body_start, key_len = sizeof(_body_start) - 1, parts = _NONE;

		if(key != NULL)
		{
			if(ret + key_len < len)
			{
				if(_match(data + ret, key, key_len - 1))
				{
					/* We actuall got a content_length */
					if(parts != _NONE)
					{
						res->parts = parts;
						res->parser_state = 0;
					}
					else res->body_started = 1;
					return ret + key_len;
				}
			}
			else
			{
				if(_match(data + ret, key, len - ret))
				{
					/* This is a partially matched one */
					res->remaining_key = key + len - ret;
					res->remaining_key_len = (uint8_t)(key_len - (len - ret));
					return len;
				}
			}
		}
	}

	return len;
}

int http_response_parse(http_response_t* res, const char* data, size_t len)
{
	if(res->response_completed)
		return -1;

	for(;len > 0;)
	{
		size_t parsed = 0;
		if(!res->body_started)
		{
			switch(res->size_determined ? _NONE : res->parts)
			{
				case _NONE:
					parsed = _detect_header(res, data, len);
					break;
				case _CL:
					parsed = _parse_content_length(res, data, len);
					break;
				case _TE:
					parsed = _parse_transfer_encoding(res, data, len);
					break;
				default:
					ERROR_RETURN_LOG(int, "Code bug: Invalid parser state");
			}
		}
		else
		{
			if(res->chunked)
			{
				if(res->parts == _CH) 
				{
					parsed = _parse_chunk_size(res, data, len);
					if(ERROR_CODE(size_t) != parsed && res->chunk_remaining == 0)
					{
						res->response_completed = 1;
						return 1;
					}
				}
				else
				{
					parsed = res->chunk_remaining;
					if(parsed > len) parsed = len;

					res->chunk_remaining -= parsed;

					if(res->chunk_remaining == 0) res->parts = _CH, res->parser_state = 0;
				}
			}
			else
			{

				parsed = res->chunk_remaining;
				if(parsed > len) parsed = len;

				res->chunk_remaining -= parsed;
				if(res->chunk_remaining == 0)
				{
					res->response_completed = 1;
					return 1;
				}
			}
		}
		if(parsed == ERROR_CODE(size_t)) return 0;	
		len -= parsed;
		data += parsed;
	}

	return 1;
}
