package test;

import java.lang.management.ManagementFactory;
import java.nio.ByteBuffer;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;

import jattach.Jattach;

public class Main {
	
	public static void main(String[] args) {
		if (args.length < 2) {
			System.out.println("Usage java -jar jattach.jar <pid> <cmd> [options] [--bufferSize <size>]");
			return;
		}
		ByteBuffer buffer = null;
		ArrayList<String> argsList = new ArrayList<>(args.length);
		for (int i = 0; i < args.length; i++) {
			if (args[i].equalsIgnoreCase("--bufferSize")) {
				if (i == args.length - 1) {
					System.err.println("buffer size not specified, use --bufferSize <size>.");
					return;
				}
				try {
					int bufferSize = Integer.parseInt(args[++i]);
					if (bufferSize >= 0) {
						buffer = ByteBuffer.allocateDirect(bufferSize);
					}
				} catch (NumberFormatException e) {
					System.err.println("Error parsing bufferSize.");
				}
			} else {
				argsList.add(args[i]);
			}
		}
		int pid;
		if (argsList.get(0).equalsIgnoreCase("crt")) {
			String vmName = ManagementFactory.getRuntimeMXBean().getName();
			pid = Integer.parseInt(vmName.substring(0, vmName.indexOf('@')));
		} else {
			pid = Integer.parseInt(argsList.get(0));
		}
		String cmd = argsList.get(1);
		String option1 = argsList.size() > 2 ? argsList.get(2) : null;
		String option2 = argsList.size() > 3 ? argsList.get(3) : null;
		String option3 = argsList.size() > 4 ? argsList.get(4) : null;
		int response = Jattach.exec(pid, cmd, option1, option2, option3, buffer);
		if (buffer != null) {
			System.out.println("Response = " + response);
			System.out.println(byteBufferToString(buffer, StandardCharsets.UTF_8));
		}
	}

	public static String byteBufferToString(ByteBuffer buffer, Charset charset) {
		buffer.flip();
		byte[] bytes = new byte[buffer.remaining()];
		buffer.get(bytes);
		String string = new String(bytes, charset);
		return string;
	}
}
