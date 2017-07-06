package info.haohou.pservlet;
import java.util.*;

public class Pservlet {
	public static native int pipeDefine(String name, int flags, String type_expr);
	public static native Byte[] pipeRead(int pipe, int nbytes);
	public static native int pipeWrite(int pipe, Byte[] data);
	public static native int pipeWriteScopeToken(int pipe, int scope_token);
	public static native int taskId();
	public static native void logWrite(int level, String message);
	public static native boolean pipeEof(int pipe);
	public static native String version();

	private static native HashMap<String, Integer> _getConst();

	private static final HashMap<String, Integer> _constants = _getConst();

	public static final int PIPE_INPUT = _constants.get("PIPE_INPUT").intValue();

	public static final int PIPE_OUTPUT = _constants.get("PIPE_OUTPUT").intValue();

	public static final int PIPE_ASYNC = _constants.get("PIPE_ASYNC").intValue();

	public static final int PIPE_SHADOW = _constants.get("PIPE_SHADOW").intValue();

};
