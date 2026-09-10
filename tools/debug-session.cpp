// Native Windows debugger for a bounded interactive Liberty session.
// Uses the same debugger events as a conventional debugger; never swallows faults.
#include <windows.h>
#include <cstdio>
#include <string>
#include <dbghelp.h>

void PrintStack(HANDLE process, DWORD threadId) {
    SymRefreshModuleList(process);
    HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION, FALSE, threadId);
    if (!thread) return;
    CONTEXT context{}; context.ContextFlags = CONTEXT_FULL;
    if (GetThreadContext(thread, &context)) {
        STACKFRAME64 frame{};
        frame.AddrPC.Offset = context.Rip; frame.AddrPC.Mode = AddrModeFlat;
        frame.AddrStack.Offset = context.Rsp; frame.AddrStack.Mode = AddrModeFlat;
        frame.AddrFrame.Offset = context.Rbp; frame.AddrFrame.Mode = AddrModeFlat;
        for (int index = 0; index < 12; ++index) {
            if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                nullptr, SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) break;
            IMAGEHLP_MODULE64 module{}; module.SizeOfStruct = sizeof(module);
            SymGetModuleInfo64(process, frame.AddrPC.Offset, &module);
            alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + MAX_SYM_NAME]{};
            auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO); symbol->MaxNameLen = MAX_SYM_NAME;
            DWORD64 offset = 0;
            const BOOL named = SymFromAddr(process, frame.AddrPC.Offset, &offset, symbol);
            std::printf("  %s!%s +0x%llx\n", module.ModuleName, named ? symbol->Name : "?", named ? offset : frame.AddrPC.Offset - module.BaseOfImage);
        }
    }
    CloseHandle(thread);
}

int wmain(int argc, wchar_t** argv) {
    if (argc < 2) return 2;
    std::wstring command = L"\"" + std::wstring(argv[1]) + L"\"";
    if (argc > 2) command += L" " + std::wstring(argv[2]);
    const ULONGLONG duration = argc > 3 ? std::wcstoul(argv[3], nullptr, 10) * 1000ULL : 180000;
    STARTUPINFOW startup{sizeof(startup)};
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(argv[1], command.data(), nullptr, nullptr, FALSE,
        DEBUG_ONLY_THIS_PROCESS, nullptr, nullptr, &startup, &process)) {
        std::printf("CREATE_FAILED %lu\n", GetLastError()); return 2;
    }
    DebugSetProcessKillOnExit(FALSE);
    SymSetOptions(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME);
    SymInitialize(process.hProcess, nullptr, TRUE);
    std::printf("DEBUG_ATTACHED pid=%lu\n", process.dwProcessId);
    std::fflush(stdout);
    const ULONGLONG started = GetTickCount64();
    unsigned exceptions = 0, fatal = 0;
    bool exited = false, initialBreakpoint = true;
    while (GetTickCount64() - started < duration) {
        DEBUG_EVENT event{};
        if (!WaitForDebugEvent(&event, 250)) continue;
        DWORD continuation = DBG_CONTINUE;
        switch (event.dwDebugEventCode) {
        case EXCEPTION_DEBUG_EVENT: {
            const DWORD code = event.u.Exception.ExceptionRecord.ExceptionCode;
            if (initialBreakpoint && code == EXCEPTION_BREAKPOINT) initialBreakpoint = false;
            else {
                ++exceptions;
                if (!event.u.Exception.dwFirstChance) ++fatal;
                std::printf("EXCEPTION code=0x%08lx firstChance=%lu address=%p\n", code,
                    event.u.Exception.dwFirstChance, event.u.Exception.ExceptionRecord.ExceptionAddress);
                if (exceptions <= 3 || !event.u.Exception.dwFirstChance) PrintStack(process.hProcess, event.dwThreadId);
                continuation = DBG_EXCEPTION_NOT_HANDLED;
            }
            break;
        }
        case CREATE_PROCESS_DEBUG_EVENT:
            if (event.u.CreateProcessInfo.hFile) CloseHandle(event.u.CreateProcessInfo.hFile);
            if (event.u.CreateProcessInfo.hProcess != process.hProcess) CloseHandle(event.u.CreateProcessInfo.hProcess);
            if (event.u.CreateProcessInfo.hThread != process.hThread) CloseHandle(event.u.CreateProcessInfo.hThread);
            break;
        case CREATE_THREAD_DEBUG_EVENT: CloseHandle(event.u.CreateThread.hThread); break;
        case LOAD_DLL_DEBUG_EVENT:
            if (event.u.LoadDll.hFile) CloseHandle(event.u.LoadDll.hFile);
            break;
        case OUTPUT_DEBUG_STRING_EVENT: {
            const auto& output = event.u.DebugString;
            if (output.nDebugStringLength < 512) {
                wchar_t wide[512]{};
                char narrow[512]{};
                if (output.fUnicode) {
                    ReadProcessMemory(process.hProcess, output.lpDebugStringData, wide,
                        output.nDebugStringLength * sizeof(wchar_t), nullptr);
                    if (wcsstr(wide, L"Liberty:")) std::printf("APP_DEBUG %ls", wide);
                } else {
                    ReadProcessMemory(process.hProcess, output.lpDebugStringData, narrow, output.nDebugStringLength, nullptr);
                    if (strstr(narrow, "Liberty:")) std::printf("APP_DEBUG %s", narrow);
                }
            }
            break;
        }
        case EXIT_PROCESS_DEBUG_EVENT:
            std::printf("PROCESS_EXIT code=%lu\n", event.u.ExitProcess.dwExitCode);
            exited = true;
            break;
        }
        ContinueDebugEvent(event.dwProcessId, event.dwThreadId, continuation);
        std::fflush(stdout);
        if (exited) break;
    }
    if (!exited) DebugActiveProcessStop(process.dwProcessId);
    SymCleanup(process.hProcess);
    std::printf("DEBUG_FINISHED exceptions=%u fatal=%u exited=%d elapsedMs=%llu\n",
        exceptions, fatal, exited, GetTickCount64() - started);
    CloseHandle(process.hThread); CloseHandle(process.hProcess);
    return fatal ? 1 : 0;
}
