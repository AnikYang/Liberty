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

struct DailyShutdownTaskStatus {
    bool exists = false;
    bool enabled = false;
    TASK_STATE state = TASK_STATE_UNKNOWN;
    std::vector<WORD> times;
    DATE nextRun = 0;
    DATE lastRun = 0;
    LONG lastResult = S_OK;
    std::wstring executable;
    std::wstring arguments;
};

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

inline bool ParseDailyShutdownBoundary(std::wstring_view boundary, WORD& minuteOfDay) {
    const size_t separator = boundary.find(L'T');
    if (separator == std::wstring_view::npos || separator + 6 > boundary.size()) return false;
    const wchar_t h0 = boundary[separator + 1], h1 = boundary[separator + 2];
    const wchar_t colon = boundary[separator + 3];
    const wchar_t m0 = boundary[separator + 4], m1 = boundary[separator + 5];
    if (h0 < L'0' || h0 > L'9' || h1 < L'0' || h1 > L'9' || colon != L':' ||
        m0 < L'0' || m0 > L'9' || m1 < L'0' || m1 > L'9') return false;
    const WORD hour = static_cast<WORD>((h0 - L'0') * 10 + h1 - L'0');
    const WORD minute = static_cast<WORD>((m0 - L'0') * 10 + m1 - L'0');
    if (hour > 23 || minute > 59) return false;
    minuteOfDay = static_cast<WORD>(hour * 60 + minute);
    return true;
}

inline HRESULT QueryDailyShutdownTask(DailyShutdownTaskStatus& status) {
    using Microsoft::WRL::ComPtr;
    status = {};
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
    ComPtr<IRegisteredTask> task;
    result = root->GetTask(name, &task);
    SysFreeString(name);
    constexpr HRESULT taskNotFound = static_cast<HRESULT>(0x8004130FL);
    if (result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) || result == taskNotFound) return S_OK;
    if (FAILED(result)) return result;
    status.exists = true;
    VARIANT_BOOL enabled = VARIANT_FALSE;
    if (FAILED(result = task->get_Enabled(&enabled)) || FAILED(result = task->get_State(&status.state))) return result;
    status.enabled = enabled == VARIANT_TRUE && status.state != TASK_STATE_DISABLED;
    task->get_NextRunTime(&status.nextRun);
    task->get_LastRunTime(&status.lastRun);
    task->get_LastTaskResult(&status.lastResult);

    ComPtr<ITaskDefinition> definition;
    if (FAILED(result = task->get_Definition(&definition))) return result;
    ComPtr<ITriggerCollection> triggers;
    if (FAILED(result = definition->get_Triggers(&triggers))) return result;
    LONG triggerCount = 0;
    if (FAILED(result = triggers->get_Count(&triggerCount))) return result;
    for (LONG index = 1; index <= triggerCount; ++index) {
        ComPtr<ITrigger> trigger;
        if (FAILED(result = triggers->get_Item(index, &trigger))) return result;
        TASK_TRIGGER_TYPE2 type = TASK_TRIGGER_EVENT;
        VARIANT_BOOL triggerEnabled = VARIANT_FALSE;
        if (FAILED(trigger->get_Type(&type)) || FAILED(trigger->get_Enabled(&triggerEnabled)) ||
            type != TASK_TRIGGER_DAILY || triggerEnabled != VARIANT_TRUE) continue;
        BSTR boundary = nullptr;
        if (FAILED(result = trigger->get_StartBoundary(&boundary))) return result;
        WORD minute = 0;
        const bool parsed = boundary && ParseDailyShutdownBoundary(boundary, minute);
        if (boundary) SysFreeString(boundary);
        if (parsed) status.times.push_back(minute);
    }
    if (!status.times.empty() && !NormalizeDailyShutdownTimes(status.times)) status.times.clear();

    ComPtr<IActionCollection> actions;
    if (SUCCEEDED(definition->get_Actions(&actions))) {
        LONG actionCount = 0;
        if (SUCCEEDED(actions->get_Count(&actionCount)) && actionCount > 0) {
            ComPtr<IAction> action;
            if (SUCCEEDED(actions->get_Item(1, &action))) {
                ComPtr<IExecAction> exec;
                if (SUCCEEDED(action.As(&exec))) {
                    BSTR path = nullptr, arguments = nullptr;
                    if (SUCCEEDED(exec->get_Path(&path)) && path) status.executable.assign(path, SysStringLen(path));
                    if (SUCCEEDED(exec->get_Arguments(&arguments)) && arguments) status.arguments.assign(arguments, SysStringLen(arguments));
                    if (path) SysFreeString(path);
                    if (arguments) SysFreeString(arguments);
                }
            }
        }
    }
    return S_OK;
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
