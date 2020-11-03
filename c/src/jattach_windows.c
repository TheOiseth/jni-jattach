/*
 * Copyright 2016 Andrei Pangin
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Modifications copyright (C) 2020 TheOiseth
 */

#include <stdio.h>
#include <stdlib.h>
#include <Windows.h>
#include "jattach.h"


const int EMPTY_COMMAND = -1000000;
const int COULD_NOT_CREATE_PIPE = -1000001;
const int NOT_ENOUGH_PRIVILEGES = -1000002;
const int COULD_NOT_OPEN_PROCESS = -1000003;
const int COULD_NOT_ALLOCATE_MEMORY = -1000004;
const int COULD_NOT_CREATE_REMOTE_THREAD = -1000005;
const int CANNOT_ATTACH_x64_TO_x32 = -1000006;
const int CANNOT_ATTACH_x32_TO_x64 = -1000007;
const int ATTACH_IS_NOT_SUPPORTED_BY_THE_TARGET_PROCESS = -1000008;
const int ERROR_READING_RESPONSE = -1000009;
const int WRONG_BYTEBUFFER = -1000010;
const int INVALID_PID_PROVIDED = -1000011;
const int PROCESS_NOT_FOUND = -1000012;
const int FAILED_TO_CHANGE_CREDENTIALS = -1000013;
const int COULD_NOT_START_ATTACH_MECHANISM = -1000014;
const int COULD_NOT_CONNECT_TO_SOCKET = -1000015;
const int ERROR_WRITING_TO_SOCKET = -1000016;
typedef HMODULE(WINAPI* GetModuleHandle_t)(LPCTSTR lpModuleName);
typedef FARPROC(WINAPI* GetProcAddress_t)(HMODULE hModule, LPCSTR lpProcName);
typedef int(__stdcall* JVM_EnqueueOperation_t)(char* cmd, char* arg0, char* arg1, char* arg2, char* pipename);

typedef struct {
    GetModuleHandle_t GetModuleHandleA;
    GetProcAddress_t GetProcAddress;
    char strJvm[32];
    char strEnqueue[32];
    char pipeName[MAX_PATH];
    char args[4][MAX_PATH];
} CallData;


#pragma check_stack(off)

// This code is executed in remote JVM process; be careful with memory it accesses
DWORD WINAPI remote_thread_entry(LPVOID param) {
    CallData* data = (CallData*)param;

    HMODULE libJvm = data->GetModuleHandleA(data->strJvm);
    if (libJvm == NULL) {
        return 1001;
    }

    JVM_EnqueueOperation_t JVM_EnqueueOperation = (JVM_EnqueueOperation_t)data->GetProcAddress(libJvm, data->strEnqueue + 1);
    if (JVM_EnqueueOperation == NULL) {
        // Try alternative name: _JVM_EnqueueOperation@20
        data->strEnqueue[21] = '@';
        data->strEnqueue[22] = '2';
        data->strEnqueue[23] = '0';
        data->strEnqueue[24] = 0;

        JVM_EnqueueOperation = (JVM_EnqueueOperation_t)data->GetProcAddress(libJvm, data->strEnqueue);
        if (JVM_EnqueueOperation == NULL) {
            return 1002;
        }
    }

    return (DWORD)JVM_EnqueueOperation(data->args[0], data->args[1], data->args[2], data->args[3], data->pipeName);
}

#pragma check_stack


// Allocate executable memory in remote process
static LPTHREAD_START_ROUTINE allocate_code(HANDLE hProcess) {
    SIZE_T codeSize = 1024;
    LPVOID code = VirtualAllocEx(hProcess, NULL, codeSize, MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    if (code != NULL) {
        WriteProcessMemory(hProcess, code, remote_thread_entry, codeSize, NULL);
    }
    return (LPTHREAD_START_ROUTINE)code;
}

// Allocate memory for CallData in remote process
static LPVOID allocate_data(HANDLE hProcess, char* pipeName, int argc, const char** argv) {
    CallData data;
    data.GetModuleHandleA = GetModuleHandleA;
    data.GetProcAddress = GetProcAddress;
    strcpy(data.strJvm, "jvm");
    strcpy(data.strEnqueue, "_JVM_EnqueueOperation");
    strcpy(data.pipeName, pipeName);

    int i;
    for (i = 0; i < 4; i++) {
        strcpy(data.args[i], i < argc ? argv[i] : "");
    }

    LPVOID remoteData = VirtualAllocEx(hProcess, NULL, sizeof(CallData), MEM_COMMIT, PAGE_READWRITE);
    if (remoteData != NULL) {
        WriteProcessMemory(hProcess, remoteData, &data, sizeof(data), NULL);
    }
    return remoteData;
}

// If the process is owned by another user, request SeDebugPrivilege to open it.
// Debug privileges are typically granted to Administrators.
static int enable_debug_privileges() {
    HANDLE hToken;
    if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, FALSE, &hToken)) {
        if (!ImpersonateSelf(SecurityImpersonation) ||
            !OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES, FALSE, &hToken)) {
            return 0;
        }
    }

    LUID luid;
    if (!LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid)) {
        return 0;
    }

    TOKEN_PRIVILEGES tp;
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Luid = luid;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

    BOOL success = AdjustTokenPrivileges(hToken, FALSE, &tp, sizeof(tp), NULL, NULL);
    CloseHandle(hToken);
    return success ? 1 : 0;
}

// Fail if attaching 64-bit jattach to 32-bit JVM or vice versa
static int check_bitness(HANDLE hProcess) {
#ifdef _WIN64
    BOOL targetWow64 = FALSE;
    if (IsWow64Process(hProcess, &targetWow64) && targetWow64) {
        return CANNOT_ATTACH_x64_TO_x32;
    }
#else
    BOOL thisWow64 = FALSE;
    BOOL targetWow64 = FALSE;
    if (IsWow64Process(GetCurrentProcess(), &thisWow64) && IsWow64Process(hProcess, &targetWow64)) {
        if (thisWow64 != targetWow64) {
            return CANNOT_ATTACH_x32_TO_x64;
        }
    }
#endif
    return 1;
}

// The idea of Dynamic Attach on Windows is to inject a thread into remote JVM
// that calls JVM_EnqueueOperation() function exported by HotSpot DLL
static int inject_thread(int pid, char* pipeName, int argc, const char** argv) {
    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)pid);
    if (hProcess == NULL && GetLastError() == ERROR_ACCESS_DENIED) {
        if (!enable_debug_privileges()) {
            //            print_error("Not enough privileges", GetLastError());
            //            return 0;
            return NOT_ENOUGH_PRIVILEGES;
        }
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, (DWORD)pid);
    }
    if (hProcess == NULL) {
        return COULD_NOT_OPEN_PROCESS;
    }
    int check_bitness_response = check_bitness(hProcess);
    if (check_bitness_response != 1) {
        CloseHandle(hProcess);
        return check_bitness_response;
    }

    LPTHREAD_START_ROUTINE code = allocate_code(hProcess);
    LPVOID data = code != NULL ? allocate_data(hProcess, pipeName, argc, argv) : NULL;
    if (data == NULL) {
        CloseHandle(hProcess);
        return COULD_NOT_ALLOCATE_MEMORY;
    }

    int response = 1;
    HANDLE hThread = CreateRemoteThread(hProcess, NULL, 0, code, data, 0, NULL);
    if (hThread == NULL) {
        response = COULD_NOT_CREATE_REMOTE_THREAD;
    }
    else {
        WaitForSingleObject(hThread, INFINITE);
        DWORD exitCode;
        GetExitCodeThread(hThread, &exitCode);
        if (exitCode != 0) {
            response = ATTACH_IS_NOT_SUPPORTED_BY_THE_TARGET_PROCESS;
        }
        CloseHandle(hThread);
    }

    VirtualFreeEx(hProcess, code, 0, MEM_RELEASE);
    VirtualFreeEx(hProcess, data, 0, MEM_RELEASE);
    CloseHandle(hProcess);

    return response;
}

// JVM response is read from the pipe and mirrored to stdout or directByteBuffer
static int read_response(HANDLE hPipe, JNIEnv* env, jobject directByteBuffer) {
    ConnectNamedPipe(hPipe, NULL);
    if (directByteBuffer != NULL) {
        jclass cls = (*env)->GetObjectClass(env, directByteBuffer);
        jmethodID limitId = (*env)->GetMethodID(env, cls, "limit", "()I");
        jmethodID positionId = (*env)->GetMethodID(env, cls, "position", "()I");
        jmethodID setPositionId = (*env)->GetMethodID(env, cls, "position", "(I)Ljava/nio/Buffer;");
        int position = (int)(*env)->CallIntMethod(env, directByteBuffer, positionId);
        int limit = (jint)(*env)->CallIntMethod(env, directByteBuffer, limitId);
        int remaining = limit - position;

        //if small direct byte buffer
        if (remaining < 8) {
            char buf[8];
            DWORD bytesRead;
            if (!ReadFile(hPipe, buf, sizeof(buf) - 1, &bytesRead, NULL)) {
                return ERROR_READING_RESPONSE;
            }
            buf[bytesRead] = 0;
            if (remaining > 0) {
                char* directBuf = (char*)(*env)->GetDirectBufferAddress(env, directByteBuffer);
                int to = remaining < bytesRead ? remaining : bytesRead;
                for (int i = 0; i < to; i++) {
                    directBuf[position++] = buf[i];
                }
                (*env)->CallObjectMethod(env, directByteBuffer, setPositionId, position);
            }
            int result = atoi(buf);
            return result;
        }

        char* buf = (char*)(*env)->GetDirectBufferAddress(env, directByteBuffer);

        DWORD bytesRead;
        if (!ReadFile(hPipe, buf + position, remaining, &bytesRead, NULL)) {
            return ERROR_READING_RESPONSE;
        }

        buf[bytesRead] = 0;
        int result = atoi(buf);

        do {
            position += bytesRead;
            remaining = limit - position;
        } while (remaining > 0 && ReadFile(hPipe, buf + position, remaining, &bytesRead, NULL));

        (*env)->CallObjectMethod(env, directByteBuffer, setPositionId, position);
        return result;
    }


    char buf[8192];
    DWORD bytesRead;
    if (!ReadFile(hPipe, buf, sizeof(buf) - 1, &bytesRead, NULL)) {
        return ERROR_READING_RESPONSE;
    }

    // First line of response is the command result code
    buf[bytesRead] = 0;
    int result = atoi(buf);

    do {
        fwrite(buf, 1, bytesRead, stdout);
    } while (ReadFile(hPipe, buf, sizeof(buf), &bytesRead, NULL));

    return result;
}

JNIEXPORT jint JNICALL Java_jattach_Jattach_exec(JNIEnv* env, jclass cl, jint jpid, jstring cmd, jstring option1, jstring option2, jstring option3, jobject directByteBuffer) {
    int pid = (int)jpid;
    if (cmd == NULL) {
        return EMPTY_COMMAND;
    }
    if (directByteBuffer != NULL) {
        jclass bufferClass = (*env)->GetObjectClass(env, directByteBuffer);
        jclass dbClass = (*env)->FindClass(env, "java/nio/DirectByteBuffer");
        if (!(*env)->IsSameObject(env, bufferClass, dbClass)) {
            return WRONG_BYTEBUFFER;
        }
    }
    char pipeName[MAX_PATH];
    sprintf(pipeName, "\\\\.\\pipe\\javatool%d", GetTickCount());
    HANDLE hPipe = CreateNamedPipe(pipeName, PIPE_ACCESS_INBOUND, PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
        1, 4096, 8192, NMPWAIT_USE_DEFAULT_WAIT, NULL);
    if (hPipe == NULL) {
        return COULD_NOT_CREATE_PIPE;
    }

    const char* nativeCmd = (*env)->GetStringUTFChars(env, cmd, 0);
    const char* nativeOption1 = option1 == NULL ? "" : (*env)->GetStringUTFChars(env, option1, 0);
    const char* nativeOption2 = option2 == NULL ? "" : (*env)->GetStringUTFChars(env, option2, 0);
    const char* nativeOption3 = option3 == NULL ? "" : (*env)->GetStringUTFChars(env, option3, 0);
    const char* arr[] = { nativeCmd, nativeOption1, nativeOption2, nativeOption3 };
    const char** argv = arr;

    int inject_thread_response = inject_thread(pid, pipeName, 4, argv);
    if (inject_thread_response != 1) {
        CloseHandle(hPipe);
        (*env)->ReleaseStringUTFChars(env, cmd, nativeCmd);
        if (option1 != NULL) {
            (*env)->ReleaseStringUTFChars(env, option1, nativeOption1);
        }
        if (option2 != NULL) {
            (*env)->ReleaseStringUTFChars(env, option2, nativeOption2);
        }
        if (option3 != NULL) {
            (*env)->ReleaseStringUTFChars(env, option3, nativeOption3);
        }
        return inject_thread_response;
    }
    if (directByteBuffer == NULL) {
        printf("Response code = ");
        fflush(stdout);
    }
    int result = read_response(hPipe, env, directByteBuffer);
    if (directByteBuffer == NULL) {
        printf("\n");
    }
    CloseHandle(hPipe);
    (*env)->ReleaseStringUTFChars(env, cmd, nativeCmd);
    if (option1 != NULL) {
        (*env)->ReleaseStringUTFChars(env, option1, nativeOption1);
    }
    if (option2 != NULL) {
        (*env)->ReleaseStringUTFChars(env, option2, nativeOption2);
    }
    if (option3 != NULL) {
        (*env)->ReleaseStringUTFChars(env, option3, nativeOption3);
    }
    return result;
}