/**
 * Copyright (C) 2017, Hao Hou
 **/
package info.haohou.pservlet;

/**
 * The general java exception
 **/
public class FrameworkException extends Exception {

	/**
	 * The reason code type
	 **/
	public enum Reason {
		PIPE_DEFINE
	};

	/**
	 * The reason code for current exceptin
	 **/
	Reason _reason;

	/**
	 * Constructor
	 * @param message The message in the exception
	 **/
	public FrameworkException(String message, Reason reason) 
	{
		super("Plumber Framework Exception: " + message);
		_reason = reason;
	}

	/**
	 * What causes this exception?
	 * @return The reason code
	 **/
	public Reason getReason()
	{
		return _reason;
	}
}
