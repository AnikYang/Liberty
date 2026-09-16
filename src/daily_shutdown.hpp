#pragma once

#include <windows.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <algorithm>
#include <string>
#include <vector>

namespace liberty {

constexpr size_t kMaxDailyShutdownTimes = 24;
constexpr wchar_t kDailyShutdownTaskName[] = L"Liberty by Bada - Daily Shutdown";
constexpr wchar_t kDailyShutdownArgument[] = L"--daily-shutdown";

inline bool NormalizeDailyShutdownTimes(std::vector<WORD>& times) {
    if (times.empty()) return false;
    if (std::any_of(times.begin(), times.end(), [](WORD value) { return value >= 24 * 60; })) return false;
    std::sort(times.begin(), times.end());
    times.erase(std::unique(times.begin(), times.end()), times.end());
    return !times.empty() && times.size() <= kMaxDailyShutdownTimes;
}

inline std::wstring DailyShutdownTimeLabel(WORD minuteOfDay) {
    wchar_t buffer[6]{};
    swprintf_s(buffer, L"%02u:%02u", minuteOfDay / 60, minuteOfDay % 60);
    return buffer;
}

inline std::wstring DailyShutdownBoundary(WORD minuteOfDay, const SYSTEMTIME& localDate) {
    wchar_t buffer[32]{};
    swprintf_s(buffer, L"%04u-%02u-%02uT%02u:%02u:00", localDate.wYear, localDate.wMonth,
        localDate.wDay, minuteOfDay / 60, minuteOfDay % 60);
    return buffer;
}

inline HRESULT DeleteDailyShutdownTask() {
    using Microsoft::WRL::ComPtr;
    ComPtr<ITaskService> service;
    HRESULT result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&service));
    if (FAILED(result)) return result;
    VARIANT empty{};
    VariantInit(&empty);
    result = service->Connect(empty, empty, empty, empty);
    if (FAILED(result)) return result;
    ComPtr<ITaskFolder> root;
    BSTR rootPath = SysAllocString(L"\\");
    if (!rootPath) return E_OUTOFMEMORY;
    result = service->GetFolder(rootPath, &root);
    SysFreeString(rootPath);
    if (FAILED(result)) return result;
    BSTR name = SysAllocString(kDailyShutdownTaskName);
    if (!name) return E_OUTOFMEMORY;
    result = root->DeleteTask(name, 0);
    SysFreeString(name);
    return result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? S_OK : result;
}

inline HRESULT ApplyDailyShutdownTask(std::vector<WORD> times, const std::wstring& executable) {
    using Microsoft::WRL::ComPtr;
    if (!NormalizeDailyShutdownTimes(times) || executable.empty()) return E_INVALIDARG;

    ComPtr<ITaskService> service;
    HRESULT result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&service));
    if (FAILED(result)) return result;
    VARIANT empty{};
    VariantInit(&empty);
    result = service->Connect(empty, empty, empty, empty);
    if (FAILED(result)) return result;

    ComPtr<ITaskFolder> root;
    BSTR rootPath = SysAllocString(L"\\");
    if (!rootPath) return E_OUTOFMEMORY;
    result = service->GetFolder(rootPath, &root);
    SysFreeString(rootPath);
    if (FAILED(result)) return result;

    ComPtr<ITaskDefinition> definition;
    result = service->NewTask(0, &definition);
    if (FAILED(result)) return result;

    ComPtr<IRegistrationInfo> registration;
    result = definition->get_RegistrationInfo(&registration);
    if (FAILED(result)) return result;
    BSTR author = SysAllocString(L"Liberty by Bada");
    if (!author) return E_OUTOFMEMORY;
    result = registration->put_Author(author);
    SysFreeString(author);
    if (FAILED(result)) return result;

    ComPtr<IPrincipal> principal;
    result = definition->get_Principal(&principal);
    if (FAILED(result)) return result;
    if (FAILED(result = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
        FAILED(result = principal->put_RunLevel(TASK_RUNLEVEL_LUA))) return result;

    ComPtr<ITaskSettings> settings;
    result = definition->get_Settings(&settings);
    if (FAILED(result)) return result;
    if (FAILED(result = settings->put_Enabled(VARIANT_TRUE)) ||
        FAILED(result = settings->put_StartWhenAvailable(VARIANT_FALSE)) ||
        FAILED(result = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
        FAILED(result = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
        FAILED(result = settings->put_WakeToRun(VARIANT_FALSE)) ||
        FAILED(result = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW))) return result;

    SYSTEMTIME today{};
    GetLocalTime(&today);
    ComPtr<ITriggerCollection> triggers;
    result = definition->get_Triggers(&triggers);
    if (FAILED(result)) return result;
    for (size_t index = 0; index < times.size(); ++index) {
        ComPtr<ITrigger> trigger;
        result = triggers->Create(TASK_TRIGGER_DAILY, &trigger);
        if (FAILED(result)) return result;
        ComPtr<IDailyTrigger> daily;
        result = trigger.As(&daily);
        if (FAILED(result)) return result;
        const std::wstring boundary = DailyShutdownBoundary(times[index], today);
        BSTR start = SysAllocString(boundary.c_str());
        if (!start) return E_OUTOFMEMORY;
        result = daily->put_StartBoundary(start);
        SysFreeString(start);
        if (FAILED(result) || FAILED(result = daily->put_DaysInterval(1)) ||
            FAILED(result = daily->put_Enabled(VARIANT_TRUE))) return result;
        const std::wstring triggerId = L"Daily-" + DailyShutdownTimeLabel(times[index]);
        BSTR id = SysAllocString(triggerId.c_str());
        if (!id) return E_OUTOFMEMORY;
        result = daily->put_Id(id);
        SysFreeString(id);
        if (FAILED(result)) return result;
    }

    ComPtr<IActionCollection> actions;
    result = definition->get_Actions(&actions);
    if (FAILED(result)) return result;
    ComPtr<IAction> action;
    result = actions->Create(TASK_ACTION_EXEC, &action);
    if (FAILED(result)) return result;
    ComPtr<IExecAction> executableAction;
    result = action.As(&executableAction);
    if (FAILED(result)) return result;
    BSTR path = SysAllocString(executable.c_str());
    BSTR arguments = SysAllocString(kDailyShutdownArgument);
    if (!path || !arguments) {
        if (path) SysFreeString(path);
        if (arguments) SysFreeString(arguments);
        return E_OUTOFMEMORY;
    }
    result = executableAction->put_Path(path);
    if (SUCCEEDED(result)) result = executableAction->put_Arguments(arguments);
    SysFreeString(path);
    SysFreeString(arguments);
    if (FAILED(result)) return result;

    BSTR taskName = SysAllocString(kDailyShutdownTaskName);
    if (!taskName) return E_OUTOFMEMORY;
    ComPtr<IRegisteredTask> registered;
    result = root->RegisterTaskDefinition(taskName, definition.Get(), TASK_CREATE_OR_UPDATE,
        empty, empty, TASK_LOGON_INTERACTIVE_TOKEN, empty, &registered);
    SysFreeString(taskName);
    return result;
}

} // namespace liberty
