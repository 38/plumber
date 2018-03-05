/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The RLS stream processor that handles the chunked transfer encoding
 * @file render/include/chunked.h
 **/
#ifndef __CHUNKED_H__
#define __CHUNKED_H__

/**
 * @brief Apply the chunked encoding transformer to the given RLS
 * @param token The token to apply
 * @param chunked_pages Indicates how many pages we can use for a single chunck
 * @return the new RLS for transformed stream
 **/
scope_token_t chunked_encode(scope_token_t token, uint8_t chunked_pages);

#endif
