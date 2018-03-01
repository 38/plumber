/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The RLS stream processor that handles the chuncked transfer encoding
 * @file encoder/include/chuncked.h
 **/
#ifndef __CHUNCKED_H__
#define __CHUNCKED_H__

/**
 * @brief Apply the chuncked encoding transformer to the given RLS
 * @param token The token to apply
 * @param chuncked_pages Indicates how many pages we can use for a single chunck
 * @return the new RLS for transformed stream
 **/
scope_token_t chuncked_encode(scope_token_t token, uint8_t chuncked_pages);

#endif
