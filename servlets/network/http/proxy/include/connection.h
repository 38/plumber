/**
 * Copyright (C) 2018, Hao Hou
 **/
/**
 * @brief The connection management utilities for the proxy servlet
 * @file proxy/include/connection.h
 **/
#ifndef __CONNECTION_H__
#define __CONNECTION_H__

/**
 * @brief The connection pool initializaiont function (Called from each servlet)
 * @note The pool is a singleton shared between all the workers and servlets
 * @param size How many connections the pool can hold
 * @param peer_pool_size How many connections can be hold to the same peer
 * @return status code
 **/
int connection_pool_init(uint32_t size, uint32_t peer_pool_size);

/**
 * @brief The connection pool finalization (Called from each servlet)
 * @return status code
 **/
int connection_pool_finalize(void);

/**
 * @brief Acquire a connection from the connection pool
 * @param hostname The destination host name
 * @param port The destination port
 * @return The socket FD that is connected the target
 **/
int connection_pool_checkout(const char* hostname, uint16_t port);

/**
 * @brief Release the connection and return it to the connection pool
 * @param hostname The peer hostname
 * @param port The port 
 * @param fd The socket FD to release
 * @param faulty Indicates if this fd is faulty and should be closed
 * @return status code
 **/
int connection_pool_checkin(const char* hostname, uint16_t port, int fd, int faulty);

#endif
