/**
 * Copyright (C) 2017, Hao Hou
 **/
/**
 * @brief The deterministic finite automata used for protocol parser with a slow IO pipe
 * @file pstd/include/pstd/dfa.h
 * @details This is the helper functions to write the protocol parser on a slow pipe like TCP pipe
 *          Because we need to handle different cases, like the pipe is waiting for data, etc.
 *          It assume the pipe supports push/pop state and follows the persist flags, otherwise
 *          this doesn't work
 **/
#ifndef __PSTD_DFA_H__
#define __PSTD_DFA_H__

/**
 * @brief The previous definition for a DFA object
 **/
typedef struct _pstd_dfa_t pstd_dfa_t;

/**
 * @brief The parameter will be passed to the processor
 **/
typedef struct {
	void*        state;   /*!< The state variable */
	pstd_dfa_t*  dfa;     /*!< Current DFA object */
	void*        data;
} pstd_dfa_process_param_t;

typedef struct {
	/**
	 * @brief Create a new DFA state
	 * @return The state has been created or NULL on error
	 **/
	void* (*create_state)(void);

	/**
	 * @brief Dispose a used DFA state
	 * @param state the state to dispose
	 * @return status code
	 **/
	int (*dispose_state)(void* state);

	/**
	 * @brief Process the next block of data
	 * @param ch the next charecter in the data stream
	 * @param param parameter
	 * return status code
	 **/
	int (*process)(char ch, pstd_dfa_process_param_t param);

	/**
	 * @brief Called when the DFA is finished
	 * @param param the param
	 * @return status code
	 **/
	int (*post_process)(pstd_dfa_process_param_t param);
} pstd_dfa_ops_t;

/**
 * @brief represent the state of the DFA
 **/
typedef enum {
	PSTD_DFA_ERROR = ERROR_CODE(int),   /*!< Something wrong with the DFA */
	PSTD_DFA_FINISHED,                  /*!< The DFA is currently stopped normally */
	PSTD_DFA_EXHUASTED,                 /*!< The DFA has exhuasted the pipe data but still not reach the finished state */
	PSTD_DFA_WAITING                    /*!< The DFA is currently waiting for more data, in this case the servlet shouldn't touch any pipe and return */
} pstd_dfa_state_t;

/**
 * @brief run DFA on the given pipe
 * @param input the input pipe
 * @param ops the operation callbacks
 * @param data additional data pass to ops functions
 * @return status code
 **/
pstd_dfa_state_t pstd_dfa_run(pipe_t input, pstd_dfa_ops_t ops, void* data);

/**
 * @brief Set the DFA to the Finished state
 * @param dfa the DFA object to set
 * @return status code
 * @note this function must be called in the DFA callbacks
 **/
int pstd_dfa_done(pstd_dfa_t* dfa);

#endif /* __PSTD_DFA_H__ */
