/**
 * Copyright (C) 2017, Hao Hou
 **/

package info.haohou.pservlet;
import java.util.*;

/**
 * The actual Pservlet API wrapper
 **/
public class Pservlet {
	/**
	 * Define a pipe 
	 * @param name The name of the pipe
	 * @param flags The flags of the pipe
	 * @param type_expr The type expression
	 * @return status code
	 **/
	public static PipeId pipeDefine(String name, int flags, String type_expr) 
			throws FrameworkException, IllegalArgumentException
	{
		if(null == name || null == type_expr)
		{
			throw new IllegalArgumentException();
		}

		int result = _Pservlet.pipeDefine(name, flags, type_expr);

		if(result == -1)
		{
			throw new FrameworkException("Cannot create pipe", FrameworkException.Reason.PIPE_DEFINE);
		}

		return PipeId.fromId(result);
	}
}

/**
 * The internal language binding for java
 **/
class _Pservlet {
	/**
	 * Define a pipe
	 * @param name The name of the pipe
	 * @param flags The flags to the pipe
	 * @param type_expr The type expression
	 * @return The pipe id
	 **/
	public static native int pipeDefine(String name, int flags, String type_expr);

	/**
	 * Read data from the pipe
	 * @param pipe The pipe id
	 * @param nbytes How many bytes we want to read from the pipe
	 * @return The result byte array
	 **/
	public static native Byte[] pipeRead(int pipe, int nbytes);

	/**
	 * Write data to the pipe
	 * @param pipe The pipe id
	 * @param data The byte array to write
	 * @return The number of bytes has been written to pipe
	 **/
	public static native int pipeWrite(int pipe, Byte[] data);

	/**
	 * Write a scope token 
	 * @param pipe The pipe id
	 * @param scope_token The token needs to be written to pipe
	 * @return If the operation is sucessfully done
	 **/
	public static native boolean pipeWriteScopeToken(int pipe, int scope_token);

	/**
	 * Get current Task id
	 * @return The current task Id
	 **/
	public static native int taskId();

	/**
	 * Write log to the Plumber logging infrastructure
	 * @param level The level of the log
	 * @param message The message 
	 * @return nothing
	 **/
	public static native void logWrite(int level, String message);

	/**
	 * Check if we are currently at the end of the pipe
	 * @param pipe The pipe id
	 * @return The check result or error code
	 **/
	public static native int pipeEof(int pipe);

	/**
	 * Get the version number of the plumber environment
	 * @return The veresion string
	 **/
	public static native String version();

	/**
	 * Get the constants defined by the Plumber framework
	 * @return A map from string to the constants
	 **/
	private static native HashMap<String, Integer> _getConst();

	/**
	 * The object that holds the collection of Plumber framework constants
	 **/
	private static final HashMap<String, Integer> _constants = _getConst();

	/**
	 * Indicates this is an input pipe
	 **/
	public static final int PIPE_INPUT = _constants.get("PIPE_INPUT").intValue();

	/**
	 * Indicates this is an output pipe
	 **/
	public static final int PIPE_OUTPUT = _constants.get("PIPE_OUTPUT").intValue();

	/**
	 * Indicates this is an asynchronized pipe
	 **/
	public static final int PIPE_ASYNC = _constants.get("PIPE_ASYNC").intValue();

	/**
	 * Indicates this is a shadow pipe
	 **/
	public static final int PIPE_SHADOW = _constants.get("PIPE_SHADOW").intValue();


	static {
		   System.loadLibrary("java_pservlet_jni");
	}
};
