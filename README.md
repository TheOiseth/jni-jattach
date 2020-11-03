## jni-jattach

### JVM Dynamic Attach library

Is a jni implementation of the [apangin/jattach](https://github.com/apangin/jattach) utility

The utility to send commands to remote JVM via Dynamic Attach mechanism.

All-in-one **jmap + jstack + jcmd + jinfo** functionality in a single tiny program.  
No installed JDK required, works with just JRE.

This is the lightweight native version of HotSpot Attach API  
https://docs.oracle.com/javase/8/docs/jdk/api/attach/spec/


### Usage

    int responseCode = Jattach.exec(pid, command, option1, option2, option3, buffer);

Where `buffer` - DirectByteBuffer in which the response will be written, if null, the result will be written to standard stdout. Use ByteBuffer.allocateDirect(bufferSize).

[Supported commands](http://hg.openjdk.java.net/jdk8u/jdk8u/hotspot/file/812ed44725b8/src/share/vm/services/attachListener.cpp#l388):
 - **load**            : load agent library
 - **properties**      : print system properties
 - **agentProperties** : print agent properties
 - **datadump**        : show heap and thread summary. Issue: the result of this command is written to standard stdout, even if `buffer` is not null
 - **threaddump**      : dump all stack traces (like jstack)
 - **dumpheap**        : dump heap (like jmap)
 - **inspectheap**     : heap histogram (like jmap -histo)
 - **setflag**         : modify manageable VM flag
 - **printflag**       : print VM flag
 - **jcmd**            : execute jcmd command

### Examples
#### Load JVMTI agent

    int responseCode = Jattach.exec(pid, "load", "path to jvmti agent", "true" | "false", "[options]", buffer);

Where `true` means that the path is absolute, `false` - the path is relative, `options` are passed to the agent.
#### Load jar agent

    int responseCode = Jattach.exec(pid, "load", "instrument", "false", "agentJar.jar", null);

#### Get properties of runnning jvm
    
    ByteBuffer buffer = ByteBuffer.allocateDirect(8192);
    int responseCode = Jattach.exec(pid, "properties", null, null, null, buffer);
    
### Terminal usage

    $ java -jar jattach.jar <pid> <command> [options] [--buffersize <size>]
