package org.harvard.pass;

import java.io.File;

/**
 * The Java interface to PASS DPAPI
 *
 * @author Peter Macko
 */
public class DPAPI
{
	static {
		System.loadLibrary("pass-java");
		init();
	}
	
	/**
	 * Initialize the PASS DPAPI
	 */
	private static native int _init();
	
	/**
	 * Initialize the PASS DPAPI
	 */
	private static void init()
	{
	    int r = _init();
	    if (r < 0) throw new RuntimeException("Cannot initialize DPAPI, error code: " + r);
	}
	
	
	/**
	 * Open a file and return the handle
	 *
	 * @param name the filename
	 * @param forWriting whether the file should also be opened for writing
	 * @return the file handle (may need to be closed)
	 */
	public static native int openFile(String name, boolean forWriting);
	
	/**
	 * Close a file handle
	 *
	 * @param fd the file handle
	 */
	public static native void closeHandle(int fd);
	
	/**
	 * Create a phony P-node
	 * 
	 * @param adj the adjacent node
	 * @return the descriptor of a created node
	 */
	private static native int _createNode(int adj);
	
	/**
	 * Create a phony P-node
	 * 
	 * @param adj the adjacent node
	 * @return the descriptor of a created node
	 */
	public static int createNode(int adj)
	{
	    int r = _createNode(adj);
	    if (r < 0) throw new RuntimeException("Cannot create a P-Node, error code: " + r);
	    return r;
	}
	
	/**
	 * Create a phony P-node
	 * 
	 * @return the descriptor of a created node
	 */
	public static int createNode() { return createNode(-1); }
	
	/**
	 * Freeze a P-node
	 * 
	 * @param fd the p-node handle or the file descriptor
	 */
	public static native void freeze(int fd);
	
	/**
	 * Add an ancestry cross-reference
	 * 
	 * @param fd the p-node to add the reference to
	 * @param key the key of the reference
	 * @param xref the cross-reference p-node
	 */
	public static native void addXRef(int fd, String key, int xref);
	
	/**
	 * Add an ancestry cross-reference
	 * 
	 * @param fd the p-node to add the reference to
	 * @param key the key of the reference
	 * @param xref the cross-reference p-node
	 * @param ver the version of the cross-referenced p-node
	 */
	public static native void addXRef(int fd, String key, int xref, int ver);
	
	/**
	 * Add an ancestry string
	 * 
	 * @param fd the p-node to add the reference to
	 * @param key the key of the reference
	 * @param xref the cross-reference p-node
	 */
	public static native void addStr(int fd, String key, String value);
}
