/**
 * Copyright (C) 2017, Hao Hou
 **/

#include <stdio.h>
#include <signal.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <dirent.h>
#include <poll.h>

#include <pservlet.h>
#include <pstd.h>

/**
 * @brief represent a process
 **/
typedef struct {
	pid_t pid;         /*!< the pid for the child process */
	int   in;          /*!< stdin */
	int   out;         /*!< stdout */
	int   err;         /*!< stderr */
	char  buf[1024];   /*!< the data buffer which contains the data read from plumber pipe */
	char* buf_b;       /*!< the start point of the buffer contains data */
	char* buf_e;       /*!< the end point of the buffer contains data */
} process_t;

/**
 * @brief servlet context
 **/
typedef struct {
	char**                args;         /*!< the command arguments */
	pipe_t                input;        /*!< the input pipe */
	pipe_t                output;       /*!< the output pipe */
	pipe_t                error;        /*!< the error output pipe */
} context_t;

/**
 * @brief close all the fd larger than max_fd
 * @note this function will be used to close all the FDs that is opened in the parent process
 *       because for the child process, it's useless to have all the parent's fd.
 * @param max_fd the maximum fd
 * @return status code
 **/
static inline int _close_fds(int max_fd)
{
	int  dirfd = open("/proc/self/fd", O_RDONLY);
	if(dirfd <= 0)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot open director /proc/self/fd");

	DIR* dir = fdopendir(dirfd);
	if(NULL == dir)
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot ceate DIR struct");

	for(;;)
	{
		errno = 0;
		struct dirent* dirent = readdir(dir);

		if(NULL == dirent)
		{
			if(errno != 0)
			    LOG_WARNING_ERRNO("Cannot readdir");

			break;
		}

		int fd = atoi(dirent->d_name);
		if(fd > max_fd && fd != dirfd && close(fd) < 0)
		    LOG_WARNING_ERRNO("Cannot close fd %d", fd);
	}

	closedir(dir);
	return 0;
}

/**
 * @brief spawn the new process for the given context
 * @param context the context we want to spawn child process for
 * @return the process data structure we are using
 **/
process_t* _spawn_process(const context_t* context)
{
	process_t* proc = (process_t*)malloc(sizeof(*proc));

	if(NULL == proc) ERROR_PTR_RETURN_LOG_ERRNO("Cannot allocate memory for the child process");

	int in_pipe[2], out_pipe[2], err_pipe[2];

	if(pipe(in_pipe) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create child stdin pipe");

	if(pipe(out_pipe) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create child stdout pipe");

	if(pipe(err_pipe) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot create child stderr pipe");

	pid_t pid = fork();
	if(pid < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot fork the process");

	if(pid == 0)
	{
		int infd  = in_pipe[0];
		int outfd = out_pipe[1];
		int errfd = err_pipe[1];

		if(dup2(infd, STDIN_FILENO) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot replace the stdin fd");

		if(dup2(outfd, STDOUT_FILENO) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot replace the stdout fd");

		if(dup2(errfd, STDERR_FILENO) < 0)
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot replace the stderr fd");

		if(ERROR_CODE(int) == _close_fds(2))
		    LOG_WARNING("Cannot close all the parent-opened FDs");

		execvp(context->args[0], context->args);

		LOG_ERROR_ERRNO("Cannot execute the command");
		exit(1);
	}
	else LOG_DEBUG("Created child process pid = %d", (int)pid);


	if(close(in_pipe[0]) < 0)
	    LOG_WARNING_ERRNO("Cannot close the unused end of the stdin pipe");

	if(close(out_pipe[1]) < 0)
	    LOG_WARNING_ERRNO("Cannot close the unused end of the stdout pipe");

	if(close(err_pipe[1]) < 0)
	    LOG_WARNING_ERRNO("Cannot close the unused end of the stderr pipe");

	proc->pid = pid;

	proc->in = in_pipe[1];
	proc->out = out_pipe[0];
	proc->err = err_pipe[0];

	if(fcntl(proc->in, F_SETFL, O_NONBLOCK) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot make the stdin file nonblocking");

	if(fcntl(proc->out, F_SETFL, O_NONBLOCK) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot make the stdout file nonblocking");

	if(fcntl(proc->err, F_SETFL, O_NONBLOCK) < 0)
	    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot make the stderr file nonblocking");

	proc->buf_b = proc->buf_e = NULL;

	return proc;

ERR:
	if(in_pipe[0] > 0) close(in_pipe[0]);
	if(in_pipe[1] > 0) close(in_pipe[1]);
	if(out_pipe[0] > 0) close(out_pipe[0]);
	if(out_pipe[1] > 0) close(out_pipe[1]);
	if(err_pipe[0] > 0) close(err_pipe[0]);
	if(err_pipe[1] > 0) close(err_pipe[1]);

	free(proc);

	return NULL;
}

/**
 * @brief wait a child process to complete
 * @param proc the process we want to wait
 * @param signal the signal we want to send to child process, 0 if we dont want to send any signal
 * @return status code
 **/
static inline int _wait_process(process_t* proc, int signal)
{
	if(signal > 0 && kill(proc->pid, signal) < 0)
	    LOG_WARNING_ERRNO("Cannot send signal to the child process");

#ifdef LOG_TRACE_ENABLED
	int status;
	if(waitpid(proc->pid, &status, 0) < 0)
#else
	if(waitpid(proc->pid, NULL, 0) < 0)
#endif
	    LOG_WARNING_ERRNO("Cannot wait for the child process to finish");

	LOG_TRACE("The child process is termanted with status code %d", status);

	int rc = 0;
	if(proc->in > 0 && close(proc->in) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the input pipe");
		rc = ERROR_CODE(int);
	}

	if(proc->out > 0 && close(proc->out) < 0)
	{
		LOG_ERROR_ERRNO("Canont close the output pipe");
		rc = ERROR_CODE(int);
	}

	if(proc->err > 0 && close(proc->err) < 0)
	{
		LOG_ERROR_ERRNO("Cannot close the error pipe");
		rc = ERROR_CODE(int);
	}

	free(proc);

	return rc;
}

static inline int _dispose_state(void* data)
{
	return _wait_process((process_t*)data, SIGHUP);
}

static inline int _init(uint32_t argc, char const* const* argv, void* ctx)
{
	uint32_t num_arg_copied;

	context_t* context = (context_t*)ctx;

	if(NULL == (context->args = (char**)malloc(sizeof(char*) * argc)))
	    ERROR_RETURN_LOG_ERRNO(int, "Cannot allocate memory for the argument array");

	if(argc < 2)
	    ERROR_RETURN_LOG(int, "Cannot start the exec servlet without param, usage exec <command-line>");

	context->args[argc - 1] = NULL;

	for(num_arg_copied = 0; num_arg_copied < argc - 1; num_arg_copied ++)
	{
		size_t len = strlen(argv[num_arg_copied + 1]);
		if(NULL == (context->args[num_arg_copied] = (char*)malloc(len + 1)))
		    ERROR_LOG_ERRNO_GOTO(ERR, "Cannot allocate memory for argument");
		memcpy(context->args[num_arg_copied], argv[num_arg_copied + 1], len + 1);
	}

	if(ERROR_CODE(pipe_t) == (context->input = pipe_define("stdin", PIPE_INPUT, NULL)))
	    ERROR_LOG_GOTO(ERR, "Cannot define the input pipe");

	if(ERROR_CODE(pipe_t) == (context->output = pipe_define("stdout", PIPE_OUTPUT | PIPE_ASYNC, NULL)))
	    ERROR_LOG_GOTO(ERR, "Cannot define the output pipe");

	if(ERROR_CODE(pipe_t) == (context->error = pipe_define("stderr", PIPE_OUTPUT | PIPE_ASYNC, NULL)))
	    ERROR_LOG_GOTO(ERR, "Cannot define the error pipe");

	return 0;

ERR:
	if(context->args != NULL)
	{
		while(num_arg_copied > 0)
		    free(context->args[-- num_arg_copied]);
		free(context->args);
	}

	return ERROR_CODE(int);
}

static inline int _cleanup(void* ctx)
{
	context_t* context = (context_t*)ctx;

	if(context->args != NULL)
	{
		int i;
		for(i = 0; context->args[i] != NULL; i ++)
		    free(context->args[i]);
		free(context->args);
	}

	return 0;
}

static inline int _exec(void* ctx)
{
	int ret = 0;
	int proc_pushed = 1;

	context_t* context = (context_t*)ctx;

	pstd_bio_t *in = NULL, *out = NULL, *err = NULL;
	process_t* proc = NULL;

	if(NULL == (in = pstd_bio_new(context->input))) ERROR_LOG_GOTO(ERR, "Cannot create input BIO object");

	if(NULL == (out = pstd_bio_new(context->output))) ERROR_LOG_GOTO(ERR, "Cannot create output BIO object");

	if(NULL == (err = pstd_bio_new(context->error))) ERROR_LOG_GOTO(ERR, "Cannot create error BIO object");


	if(pipe_cntl(context->input, PIPE_CNTL_POP_STATE, &proc) == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot pop the state from the pipe");

	if(NULL == proc)
	{
		if(NULL == (proc = _spawn_process(context)))
		    ERROR_LOG_GOTO(ERR, "Cannot create child process");
		else
		{
			LOG_DEBUG("The new child process has been spawned for current request");
			proc_pushed = 0;
		}
	}

	struct pollfd pollfds[] = {
		{
			.fd = proc->in,
			.events = POLLOUT | POLLHUP
		},
		{
			.fd = proc->out,
			.events = POLLIN | POLLHUP
		},
		{
			.fd = proc->err,
			.events = POLLIN | POLLHUP
		}
	};

	int no_more_data = 0;
	int stdin_dead = 0;
	int stdout_dead = 0;
	int stderr_dead = 0;

	for(;!stdin_dead || !stdout_dead || !stderr_dead;)
	{
		LOG_DEBUG("Start poll the FDs, stdin:%d stdout:%d stderr:%d", stdin_dead, stdout_dead, stderr_dead);
		if(poll(pollfds, sizeof(pollfds) / sizeof(pollfds[0]), -1) >= 0)
		{
			if((pollfds[0].revents & POLLHUP)) stdin_dead = 1;
			if((pollfds[1].revents & POLLHUP)) stdout_dead = 1;
			if((pollfds[2].revents & POLLHUP)) stderr_dead = 1;

			if(pollfds[0].revents & POLLOUT)
			{
				if(proc->buf_e <= proc->buf_b && !no_more_data)
				{
					LOG_DEBUG("The Plumber pipe buffer is empty, read from the plumber pipe buffer");
					size_t sz = pstd_bio_read(in, proc->buf, sizeof(proc->buf));
					if(sz == 0)
					{
						if(ERROR_CODE(int) == (no_more_data = pstd_bio_eof(in)))
						    ERROR_LOG_GOTO(ERR, "Cannot check if the input plumber pipe gets the end of stream");

						if(!no_more_data)
						{
							LOG_DEBUG("The plumber pipe is waiting for data, preserve the process state and move on");
							if(ERROR_CODE(int) == pipe_cntl(context->input, PIPE_CNTL_SET_FLAG, PIPE_PERSIST))
							    ERROR_LOG_GOTO(ERR, "Cannot set the pipe to persist mode");

							if(ERROR_CODE(int) == pipe_cntl(context->input, PIPE_CNTL_PUSH_STATE, proc, _dispose_state))
							    ERROR_LOG_GOTO(ERR, "Cannot push state");
							goto RET;
						}
						else if(close(proc->in) < 0)
						    ERROR_LOG_GOTO(ERR, "Cannot close the stdin pipe");
						else
						{
							LOG_DEBUG("Pipe stdin has been shutted down");
							stdin_dead = 1;
							proc->in = 0;
						}
					}

					proc->buf_b = proc->buf;
					proc->buf_e = proc->buf + sz;
				}

				for(;proc->buf_e > proc->buf_b;)
				{
					ssize_t written = write(proc->in, proc->buf_b, (size_t)(proc->buf_e - proc->buf_b));
					if(written == 0)
					{
						LOG_DEBUG("Pipe stdin has been shutted down by child process");
						stdin_dead = 1;
					}
					else if(written == -1)
					{
						if(errno == EAGAIN || errno == EWOULDBLOCK)
						    continue;
						else ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write to stdin pipe");
					}
					else proc->buf_b += written;
				}
			}

			int fd;
			for(fd = 1; fd <= 2; fd ++)
			{
				if(pollfds[fd].revents & POLLIN)
				{
					char buf[1024];
					for(;;)
					{
						ssize_t rdsz = read(fd == 1 ? proc->out : proc->err, buf, sizeof(buf));
						if(rdsz == 0)
						{
							LOG_DEBUG("Pipe %s has been shutted down by child process", fd == 1 ? "stdout" : "stderr");
							if(fd == 1)
							    stdout_dead = 1;
							else
							    stderr_dead = 1;

							break;
						}
						else if(rdsz < 0)
						{
							if(errno == EAGAIN || errno == EWOULDBLOCK)
							    break;
							else ERROR_LOG_ERRNO_GOTO(ERR, "Cannot write to stdin pipe");
						}
						else
						{
							size_t written = 0;
							for(;written < (size_t)rdsz;)
							{
								size_t iter_written = pstd_bio_write(fd == 1 ? out : err, buf + written, (size_t)rdsz - written);
								if(ERROR_CODE(size_t) == iter_written)
								    ERROR_LOG_GOTO(ERR, "Cannot write stdout data to output pipe");
								written += iter_written;
							}
						}
					}
				}
			}
		}
		else ERROR_LOG_ERRNO_GOTO(ERR, "Cannot poll the UNIX pipe");
	}

	LOG_DEBUG("All the pipes are shutted down, waiting the process to terminate");

	if(!proc_pushed && _wait_process(proc, 0) == ERROR_CODE(int))
	    ERROR_LOG_GOTO(ERR, "Cannot wait for the child process to complete");

	if(ERROR_CODE(int) == pipe_cntl(context->input, PIPE_CNTL_CLR_FLAG, PIPE_PERSIST))
	    ERROR_LOG_GOTO(ERR, "Cannot clear the persist flag for the input pipe");
	goto RET;
ERR:
	ret = ERROR_CODE(int);
	pipe_cntl(context->input, PIPE_CNTL_CLR_FLAG, PIPE_PERSIST);
	if(NULL != proc && !proc_pushed)
	    _wait_process(proc, SIGKILL);
RET:
	if(NULL != in) pstd_bio_free(in);
	if(NULL != out) pstd_bio_free(out);
	if(NULL != err) pstd_bio_free(err);
	return ret;
}

SERVLET_DEF = {
	.desc = "Execute a exetuable and connect its stdin, stdout, stderr with plumber pipe",
	.version = 0,
	.size = sizeof(context_t),
	.init = _init,
	.exec = _exec,
	.unload = _cleanup
};
