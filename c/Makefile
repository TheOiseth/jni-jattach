#set JDK_HOME for unix system:
#in terminal before 'make':
#export JDK_HOME=<path to the jdk folder which contains the 'include' folder>
#for example: 
#export JDK_HOME=/usr/linuxuser/jdk1.8.0
ifneq ($(findstring Windows,$(OS)),)
#Windows
  CL=cl.exe
  CFLAGS=/O2 /D_CRT_SECURE_NO_WARNINGS
  JATTACH_LIB=jattach.dll
else 
  UNAME_S:=$(shell uname -s)
  ifneq ($(findstring Darwin,$(UNAME_S)),)
#MacOSX
    CC=cc
    CFLAGS=-O2
    JATTACH_LIB=libjattach.jnilib
  else
#Linux
    CC=gcc
    CFLAGS=-O2 -shared -fpic
    JATTACH_LIB=libjattach.so
  endif
endif

all: build build/$(JATTACH_LIB)

build:
	mkdir -p build

build/libjattach.so: src/jattach_posix.c
	$(CC) $(CFLAGS) -o $@ $^ -I"$(JDK_HOME)/include" -I"$(JDK_HOME)/include/linux"

build/jattach.dll: src/jattach_windows.c
	$(CL) $(CFLAGS) /I "$(JAVA_HOME)\include" /I "$(JAVA_HOME)\include\win32" /Fobuild/jattach.obj /Fe$@ $^ advapi32.lib /link /DLL 

build/libjattach.jnilib: src/jattach_posix.c
	$(CC) $(CFLAGS) -o $@ -shared $^ -I"$(JDK_HOME)/include" -I"$(JDK_HOME)/include/darwin"

clean:
	rm -rf build
