package jattach;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.util.Random;

public class Jattach {
	public static final int EMPTY_COMMAND = -1000000;
	public static final int WRONG_BYTEBUFFER = -1000010;

	// windows os error codes
	public static final int COULD_NOT_CREATE_PIPE = -1000001;
	public static final int NOT_ENOUGH_PRIVILEGES = -1000002;
	public static final int COULD_NOT_OPEN_PROCESS = -1000003;
	public static final int COULD_NOT_ALLOCATE_MEMORY = -1000004;
	public static final int COULD_NOT_CREATE_REMOTE_THREAD = -1000005;
	public static final int CANNOT_ATTACH_x64_TO_x32 = -1000006;
	public static final int CANNOT_ATTACH_x32_TO_x64 = -1000007;
	public static final int ATTACH_IS_NOT_SUPPORTED_BY_THE_TARGET_PROCESS = -1000008;
	public static final int ERROR_READING_RESPONSE = -1000009;

	// unix os error codes
	public static final int INVALID_PID_PROVIDED = -1000011;
	public static final int PROCESS_NOT_FOUND = -1000012;
	public static final int FAILED_TO_CHANGE_CREDENTIALS = -1000013;
	public static final int COULD_NOT_START_ATTACH_MECHANISM = -1000014;
	public static final int COULD_NOT_CONNECT_TO_SOCKET = -1000015;
	public static final int ERROR_WRITING_TO_SOCKET = -1000016;

	static {
		final String jattacFileName;
		final String osName = System.getProperty("os.name").toLowerCase();
		final String jattachJarPath;

		if (osName.indexOf("win") >= 0) {
			if (System.getProperty("os.arch").indexOf("64") >= 0) {
				jattachJarPath = "native/jattach64.dll";
				jattacFileName = "jattach64.dll";
			} else {
				jattachJarPath = "native/jattach32.dll";
				jattacFileName = "jattach32.dll";
			}
		} else if (osName.indexOf("mac") >= 0) {
			jattachJarPath = "native/libjattach64.jnilib";
			jattacFileName = "libjattach64.jnilib";
		} else if (osName.indexOf("nix") >= 0 || osName.indexOf("nux") >= 0 || osName.indexOf("bsd") >= 0) {
			if (System.getProperty("os.arch").indexOf("64") >= 0) {
				jattachJarPath = "native/libjattach64.so";
				jattacFileName = "libjattach64.so";
			} else {
				jattachJarPath = "native/libjattach32.so";
				jattacFileName = "libjattach32.so";
			}
		} else {
			jattachJarPath = null;
			jattacFileName = null;
			System.err.println("This operating system is not supported");
		}
		File jattachLibFile = null;
		File jattachLibFolder = new File("native");
		if (jattachLibFolder.exists()) {
			jattachLibFile = new File(jattachLibFolder, jattacFileName);

			if (!jattachLibFile.exists()) {
				writeToFile(jattachJarPath, jattachLibFile);
			}
		} else {
			String tmpDir = System.getProperty("java.io.tmpdir");
			if (tmpDir.charAt(tmpDir.length() - 1) != '\\' && tmpDir.charAt(tmpDir.length() - 1) != '/') {
				tmpDir = tmpDir + "/";
			}
			tmpDir = tmpDir + "jni_jattach/";
			deleteDirectory(new File(tmpDir));

			int index = jattacFileName.lastIndexOf('.');
			String prefix = jattacFileName.substring(0, index);
			String suffix = jattacFileName.substring(index);
			Random r = new Random();
			jattachLibFile = new File(tmpDir + prefix + System.currentTimeMillis() + r.nextInt(100) + suffix);
			jattachLibFile.getParentFile().mkdirs();
			jattachLibFile.getParentFile().setReadable(true, false);
			jattachLibFile.getParentFile().setWritable(true, false);
			jattachLibFile.getParentFile().setExecutable(true, false);
			jattachLibFile.deleteOnExit();
			writeToFile(jattachJarPath, jattachLibFile);
			jattachLibFile.deleteOnExit();

		}
		System.load(jattachLibFile.getAbsolutePath());
	}

	private static void deleteDirectory(File path) {
		if (path.isDirectory())
			for (File file : path.listFiles())
				deleteDirectory(file);
		path.delete();
	}

	private static void writeToFile(String jattachJarPath, File jattachLibFile) {
		try {
			InputStream is = Jattach.class.getResourceAsStream('/' + jattachJarPath);
			if (is != null) {
				ByteArrayOutputStream baos = new ByteArrayOutputStream(is.available());
				FileOutputStream fos = new FileOutputStream(jattachLibFile);
				int i;
				while ((i = is.read()) != -1) {
					baos.write(i);
				}
				baos.writeTo(fos);
				fos.close();
			} else {
				System.err.println("jattach native file not found in jar");
			}
		} catch (IOException e) {
			e.printStackTrace();
		}
	}

	public static native int exec(int pid, String cmd, String arg1, String arg2, String arg3, ByteBuffer buffer);
}
