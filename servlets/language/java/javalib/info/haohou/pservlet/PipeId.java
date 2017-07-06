/**
 * Copyright (C) 2017, Hao Hou
 **/
package info.haohou.pservlet;

/**
 * The object wrapper for a pipe id
 **/
public class PipeId {

	/**
	 * The internal pipe Id
	 **/
	private final int _id;

	/**
	 * Constructor
	 **/
	private PipeId(int id) {
		_id = id;
	}

	/**
	 * Get the pipe id object from int
	 **/
	public static PipeId fromId(int id) {
		return new PipeId(id);
	}

	/**
	 * Get the pipe Id number
	 **/
	public int id() {
		return _id;
	}
}
