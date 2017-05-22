/**
 * Copyright (C) 2017, Hao Hou
 **/
#include <error.h>
#include <pservlet.h>

#include <bio.h>
#include <dfa.h>

/**
 * @brief The actual data structure for the DFA
 **/
struct _pstd_dfa_t {
	int                 done;         /*!< if the DFA is in finished state */
};

pstd_dfa_state_t pstd_dfa_run(pipe_t input, pstd_dfa_ops_t ops, void* data)
{
	pstd_dfa_state_t ret = ERROR_CODE(pstd_dfa_state_t);

	if(ERROR_CODE(pipe_t) == input ||
	   NULL == ops.create_state || NULL == ops.dispose_state || NULL == ops.process)
	    ERROR_RETURN_LOG(pstd_dfa_state_t, "Invalid arguments");

	pstd_bio_t* bio = NULL;
	void* state = NULL;
	int fresh = 0;

	if(NULL == (bio = pstd_bio_new(input)))
	    ERROR_RETURN_LOG(pstd_dfa_state_t, "Create BIO object for the input pipe");

	if(ERROR_CODE(int) == pipe_cntl(input, PIPE_CNTL_POP_STATE, &state))
	    ERROR_LOG_GOTO(RET, "Cannot pop the previously stated state");

	if(NULL == state)
	{
		if(NULL == (state = ops.create_state()))
		    ERROR_LOG_GOTO(RET, "Cannot create fresh state variable");
		fresh = 1;
	}


	pstd_dfa_t ctx = {
		.done = 0
	};

	/* Before we start, we need remove the persist flag */
	if(ERROR_CODE(int) == pipe_cntl(input, PIPE_CNTL_CLR_FLAG, PIPE_PERSIST))
	    ERROR_LOG_GOTO(RET, "Cannot remove the persist flag from the input pipe");

	for(;;)
	{
		int eof_rc = pstd_bio_eof(bio);
		if(ERROR_CODE(int) == eof_rc)
		    ERROR_LOG_GOTO(RET, "Cannot check if the pipe has no more data");

		if(eof_rc)
		{
			ret = PSTD_DFA_EXHUASTED;
			goto RET;
		}

		char ch;
		int read_rc = pstd_bio_getc(bio, &ch);
		if(ERROR_CODE(int) == read_rc)
		    ERROR_LOG_GOTO(RET, "Cannot read the data from buffer");

		if(0 == read_rc)
		{
			eof_rc = pstd_bio_eof(bio);
			if(ERROR_CODE(int) == eof_rc)
			    ERROR_LOG_GOTO(RET, "Cannot check if the pipe has no more data");

			if(eof_rc)
			{
				/* If we used up all the bytes in the pipe and can't finish, we are exahuasted */
				ret = PSTD_DFA_EXHUASTED;
				goto RET;
			}
			else
			{
				/* If we need to wait for the pipe get ready then we set to the waiting state */
				if(ERROR_CODE(int) == pipe_cntl(input, PIPE_CNTL_SET_FLAG, PIPE_PERSIST))
				    ERROR_LOG_GOTO(RET, "Cannot set the persist flag to the pipe");

				if(ERROR_CODE(int) == pipe_cntl(input, PIPE_CNTL_PUSH_STATE, state, ops.dispose_state))
				    ERROR_LOG_GOTO(RET, "Cannot push the state to the pipe");

				fresh = 0;
				ret = PSTD_DFA_WAITING;
				goto RET;
			}
		}
		else
		{
			pstd_dfa_process_param_t param = {
				.dfa = &ctx,
				.state = state,
				.data = data
			};
			if(ERROR_CODE(int) == ops.process(ch, param))
			    ERROR_LOG_GOTO(RET, "Cannot process the data");

			if(ctx.done)
			{
				if(ops.post_process != NULL && ERROR_CODE(int) == ops.post_process(param))
				    ERROR_LOG_GOTO(RET, "Cannot do post processing on the data");

				ret = PSTD_DFA_FINISHED;
				goto RET;
			}
		}
	}

RET:
	if(NULL != bio && ERROR_CODE(int) == pstd_bio_free(bio))
	    LOG_WARNING("Cannot dispose the BIO object");

	if(NULL != state &&  fresh != 0 && ERROR_CODE(int) == ops.dispose_state(state))
	    LOG_WARNING("Cannot dispose the state");

	return ret;
}

int pstd_dfa_done(pstd_dfa_t* dfa)
{
	if(NULL == dfa) ERROR_RETURN_LOG(int, "Invalid arguments");
	dfa->done = 1;
	return 0;
}
