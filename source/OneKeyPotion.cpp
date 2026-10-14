#define NOMINMAX
#include <windows.h>
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <fstream>
#include <sstream>
#include "NativeApi.h"
#include "GameNativeApi.h"
#include "PotionUi.h"
#include "KeyboardKey.h"

namespace {
HMODULE g_module = nullptr;
HWND g_game = nullptr;
DWORD g_gameThread = 0;
HHOOK g_messageHook = nullptr;
UINT g_dispatch = 0;
std::atomic<bool> g_ready{false};
std::atomic<bool> g_posted{false};
std::wstring g_ini, g_log, g_directory;
int g_hotkey = VK_F8;
int g_guiKey = VK_F9;
bool IsDrinkKey(int key) {return key>=VK_F1 && key<=VK_F24 && key!=g_guiKey;}
int DefaultDrinkKey() {return g_guiKey==VK_F8?VK_F7:VK_F8;}
bool g_keyHeld = false;
ClientPatchGameNativeApi g_native = {};
bool g_nativeBound = false;
struct Potion { int itemId = 0; std::wstring name; std::vector<int> effects, groupEffects, variousBuffs, groupBuffs; int sharedGroup=0; bool variousOnly=false; bool appendage = false; bool oneShot = false; bool bothSources = false; };
std::vector<Potion> g_potions;
std::set<int> g_unconfirmed;
std::vector<std::wstring> g_unsupportedNames;
struct Job {
    bool active = false;
    uintptr_t character = 0;
    size_t index = 0;
    bool awaiting = false;
    DWORD deadline = 0;
    DWORD next = 0;
    int used = 0, submitted = 0, skipped = 0, failed = 0;
    std::set<int> attemptedGroups;
    std::vector<std::wstring> usedNames, failedNames;
} g_job;

void Log(const wchar_t* format, ...) {
    wchar_t message[1024] = {};
    va_list arguments;
    va_start(arguments, format);
    _vsnwprintf_s(message, _countof(message), _TRUNCATE, format, arguments);
    va_end(arguments);
    SYSTEMTIME now; GetLocalTime(&now);
    wchar_t line[1200] = {};
    swprintf_s(line, L"[%02u:%02u:%02u.%03u] %s\r\n", now.wHour, now.wMinute,
        now.wSecond, now.wMilliseconds, message);
    const int bytes = WideCharToMultiByte(CP_UTF8, 0, line, -1, nullptr, 0, nullptr, nullptr);
    std::vector<char> utf8(static_cast<size_t>(bytes));
    WideCharToMultiByte(CP_UTF8, 0, line, -1, utf8.data(), bytes, nullptr, nullptr);
    HANDLE file = CreateFileW(g_log.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(file, utf8.data(), static_cast<DWORD>(bytes - 1), &written, nullptr);
        CloseHandle(file);
    }
}
void Notice(const wchar_t* text, bool initial = false) {
    if (!g_nativeBound) g_nativeBound = ClientPatchBindGameNative(&g_native);
    if (!g_nativeBound) return;
    const int color = g_native.chatRgb ? g_native.chatRgb(255, 210, 80) : 0;
    if (initial && g_native.postLoadNotice) g_native.postLoadNotice(text, color);
    else if (g_native.postChatNotice) g_native.postChatNotice(text, color);
}
bool Foreground() {
    DWORD process = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &process);
    return process == GetCurrentProcessId();
}
bool CanAct() { return Foreground() && !potion_native::TextInput() && potion_native::Character(); }
bool IsActive(const Potion& potion, bool& active, int& remaining, bool includeGroup=true) {
    active = false; remaining = 0;
    if (potion.oneShot) return true;
    bool healthy=true;
    const auto& ids=includeGroup && !potion.groupEffects.empty()?potion.groupEffects:potion.effects;
    if(!potion.variousOnly) for (int id : ids) {
        int value = 0;
        if(potion.bothSources) {
            int maintained=0, appendage=0;bool appendagePresent=false;
            const bool maintainedOK=potion_native::EffectRemaining(id,maintained);
            const bool appendageOK=potion_native::AppendageState(id,appendagePresent,appendage);
            healthy=healthy && maintainedOK && appendageOK;
            // A readable positive source proves presence. A failed source
            // cannot prove absence or invalidate another source's evidence.
            if(maintainedOK) value=maintained;
            if(appendageOK) {
                if(appendagePresent) active=true;
                value=(std::max)(value,appendage);
            }
        } else {
            const bool ok=potion.appendage?potion_native::AppendageRemaining(id,value):potion_native::EffectRemaining(id,value);
            if(!ok) {healthy=false;continue;}
        }
        remaining = (std::max)(remaining, value);
        if (value > 0) active = true;
    }
    const auto& buffs=includeGroup && !potion.groupBuffs.empty()?potion.groupBuffs:potion.variousBuffs;
    for(int id:buffs) {
        bool present=false;
        const bool ok=potion_native::VariousBuffActive(id,present);
        if(!ok) healthy=false;
        else if(present) active=true;
    }
    return active || healthy;
}
void RecoverUnconfirmed() {
    for(const auto& potion:g_potions) {
        if(!g_unconfirmed.count(potion.itemId)) continue;
        bool active=false;int remaining=0;
        // Only this medicine's own source can release its uncertainty guard.
        if(IsActive(potion,active,remaining,false) && active) {
            g_unconfirmed.erase(potion.itemId);
            Log(L"延迟药效已确认：%s (%d)，解除暂停，结束后可正常补药",potion.name.c_str(),potion.itemId);
        }
    }
}
std::vector<std::wstring> ResultNotices(const Job& job) {
    std::vector<std::wstring> parts;
    auto chunks=[&](const std::vector<std::wstring>& names,bool used) {
        std::wstring list;
        for(const auto& name:names) {
            if(!list.empty() && list.size()+name.size()+1>160) {
                parts.push_back(used?L"已使用"+list:list+L"使用失败");list.clear();
            }
            if(!list.empty()) list+=L"、";
            list+=name;
        }
        if(!list.empty()) parts.push_back(used?L"已使用"+list:list+L"使用失败");
    };
    chunks(job.usedNames,true);chunks(job.failedNames,false);
    if(parts.empty()) return {L"一键喝药：本轮无药剂使用"};
    std::vector<std::wstring> notices;std::wstring line=L"一键喝药：";
    for(const auto& part:parts) {
        if(line.size()>5 && line.size()+part.size()+1>180) {notices.push_back(line);line=L"一键喝药：";}
        if(line.size()>5) line+=L"；";
        line+=part;
    }
    notices.push_back(line);return notices;
}
void Finish() {
    Log(L"本轮结束：药效确认=%d，一次性原生提交=%d，跳过=%d，失败/未确认=%d",
        g_job.used, g_job.submitted, g_job.skipped, g_job.failed);
    for(const auto& notice:ResultNotices(g_job)) Notice(notice.c_str());
    g_job.active = false;
}
void Step() {
    if (!g_job.active) return;
    if (!CanAct() || potion_native::Character() != g_job.character) {
        Log(L"本轮中止：失去前台、进入文字输入或切换角色");
        Notice(L"补药中止：游戏失去前台、进入文字输入或切换角色");
        g_job.active = false; return;
    }
    const auto now = GetTickCount();
    if (static_cast<int>(now - g_job.next) < 0) return;
    if (g_job.index >= g_potions.size()) { Finish(); return; }
    const auto& potion = g_potions[g_job.index];
    bool active = false; int remaining = 0;
    if (!IsActive(potion, active, remaining,!g_job.awaiting)) {
        Log(L"状态读取失败：%s (%d)，跳过此药并继续后续；已提交=%d", potion.name.c_str(), potion.itemId,g_job.awaiting?1:0);
        if(g_job.awaiting) g_unconfirmed.insert(potion.itemId);
        ++g_job.failed;g_job.failedNames.push_back(potion.name);g_job.awaiting=false;++g_job.index;g_job.next=now+250;
        Notice(L"某药状态读取失败，已跳过并继续后续药剂"); return;
    }
    if (g_job.awaiting) {
        if (active) {
            ++g_job.used;g_job.usedNames.push_back(potion.name);
            if(remaining>0) Log(L"药效已确认：%s (%d)，剩余=%dms", potion.name.c_str(), potion.itemId, remaining);
            else Log(L"药效已确认：%s (%d)，无毫秒倒计时，结束以原生状态为准",potion.name.c_str(),potion.itemId);
        } else if (static_cast<int>(now - g_job.deadline) < 0) return;
        else {
            ++g_job.failed;g_job.failedNames.push_back(potion.name);
            g_unconfirmed.insert(potion.itemId);
            Log(L"使用后未确认药效：%s (%d)，暂停此药，等待自身药效确认", potion.name.c_str(), potion.itemId);
            Notice(L"某药使用后未确认药效，暂停此药，检测到自身药效后自动恢复，请查看日志");
        }
        g_job.awaiting = false; ++g_job.index; g_job.next = now + 250; return;
    }
    if (active) {
        ++g_job.skipped; ++g_job.index;
        if(remaining>0) Log(L"跳过有效药效：%s (%d)，剩余=%dms", potion.name.c_str(), potion.itemId, remaining);
        else Log(L"跳过有效药效：%s (%d)，原生状态/次数记录仍有效",potion.name.c_str(),potion.itemId);
        return;
    }
    if (!potion.oneShot && g_unconfirmed.count(potion.itemId)) {
        ++g_job.failed;g_job.failedNames.push_back(potion.name); ++g_job.index;
        Log(L"跳过未确认药效的药剂：%s (%d)",potion.name.c_str(),potion.itemId); return;
    }
    if(potion.sharedGroup>0 && g_job.attemptedGroups.count(potion.sharedGroup)) {
        ++g_job.skipped;++g_job.index;
        Log(L"本轮同组药已尝试，跳过重复请求：%s (%d)，效果组=%d",potion.name.c_str(),potion.itemId,potion.sharedGroup);return;
    }
    int slot = -1, count = 0;
    if (!potion_native::FindItem(potion.itemId, slot, count)) {
        Log(L"背包读取失败，停止本轮"); Notice(L"补药中止：背包读取失败"); g_job.active = false; return;
    }
    if (slot < 0) {
        ++g_job.failed;g_job.failedNames.push_back(potion.name); ++g_job.index;
        Log(L"缺药跳过：%s (%d)", potion.name.c_str(), potion.itemId); return;
    }
    if (!potion_native::CheckPotionUse(slot,potion.itemId)) {
        ++g_job.failed;g_job.failedNames.push_back(potion.name); ++g_job.index;
        Log(L"使用前类型校验不符，跳过：%s",potion.name.c_str());
        Notice(L"有物品未通过药剂使用类型校验，已跳过"); return;
    }
    bool cooling=false;
    if(potion_native::PotionCoolingDown(slot,potion.itemId,cooling) && cooling) {
        ++g_job.skipped;++g_job.index;
        Log(L"冷却跳过：%s (%d)",potion.name.c_str(),potion.itemId);return;
    }
    if (!potion_native::UseItem(slot,potion.itemId)) {
        if(potion_native::PotionCoolingDown(slot,potion.itemId,cooling) && cooling) ++g_job.skipped;
        else {++g_job.failed;g_job.failedNames.push_back(potion.name);}
        ++g_job.index; g_job.next = now + 250;
        Log(L"原生使用拒绝：%s (%d)，可能冷却或使用条件不满足", potion.name.c_str(), potion.itemId);
        return;
    }
    if(potion.sharedGroup>0) g_job.attemptedGroups.insert(potion.sharedGroup);
    Log(L"已进入原生使用流程：%s",potion.name.c_str());
    if (potion.oneShot) {
        ++g_job.submitted;g_job.usedNames.push_back(potion.name); ++g_job.index; g_job.next = now + 250;
        Log(L"一次性药剂已提交：%s (%d)，不等待药效；本轮不重试，原生冷却/限制仍生效",
            potion.name.c_str(), potion.itemId);
        return;
    }
    g_job.awaiting = true; g_job.deadline = now + 2000;
}
void Start() {
    if (!CanAct()) return;
    if (!g_unsupportedNames.empty()) {
        std::wstring notice=L"暂未接入自动补药：";
        for (size_t i=0;i<g_unsupportedNames.size() && i<3;++i) {
            if(i) notice+=L"、";
            notice+=g_unsupportedNames[i];
        }
        if(g_unsupportedNames.size()>3) notice+=L" 等";
        Notice(notice.c_str());
    }
    if (g_potions.empty()) { Notice(L"没有已配置的可用药剂，请打开配置窗口设置并保存"); return; }
    if (g_job.active) { Log(L"本轮尚未结束，忽略重复触发"); Notice(L"正在补药，请等待本轮结束"); return; }
    RecoverUnconfirmed();
    g_job = {};
    g_job.failedNames=g_unsupportedNames;g_job.failed=static_cast<int>(g_unsupportedNames.size());
    g_job.active = true; g_job.character = potion_native::Character(); g_job.next = GetTickCount();
    Log(L"开始一轮喝药，药品数=%zu",g_potions.size());
    Step();
}
#include "PotionController.inl"
LRESULT CALLBACK GameMessages(int code, WPARAM removing, LPARAM parameter) {
    if (code >= 0 && removing == PM_REMOVE && parameter) {
        auto* message = reinterpret_cast<MSG*>(parameter);
        if (message->message == WM_KEYUP && message->wParam == static_cast<WPARAM>(g_hotkey)) g_keyHeld = false;
        if (message->message == g_dispatch) {
            g_posted = false;
            if (g_ready) { GuiTick(); Step(); }
            message->message = WM_NULL;
        } else if (g_ready && (message->hwnd == g_game || IsChild(g_game, message->hwnd)) && GuiKey(message)) {
            message->message = WM_NULL;
        } else if (g_ready && message->message == WM_KEYDOWN &&
            message->wParam == static_cast<WPARAM>(g_hotkey) &&
            !(GetKeyState(VK_CONTROL) & 0x8000) && !(GetKeyState(VK_MENU) & 0x8000) &&
            !(GetKeyState(VK_SHIFT) & 0x8000) &&
            (message->hwnd == g_game || IsChild(g_game, message->hwnd)) && CanAct()) {
            if (!g_keyHeld && !(message->lParam & (1u << 30))) { g_keyHeld = true; Start(); }
            message->message = WM_NULL;
        }
    }
    return CallNextHookEx(g_messageHook, code, removing, parameter);
}
BOOL CALLBACK FindWindow(HWND window, LPARAM data) {
    DWORD process = 0;
    GetWindowThreadProcessId(window, &process);
    if (process != GetCurrentProcessId() || !IsWindowVisible(window) || GetParent(window)) return TRUE;
    RECT rect = {}; GetWindowRect(window, &rect);
    auto* best = reinterpret_cast<std::pair<HWND, long long>*>(data);
    const long long area = static_cast<long long>(rect.right-rect.left)*(rect.bottom-rect.top);
    if (area > best->second) *best = {window, area};
    return TRUE;
}
bool Attach() {
    if (g_game && IsWindow(g_game) && g_messageHook) return true;
    if (g_messageHook) { UnhookWindowsHookEx(g_messageHook); g_messageHook = nullptr; }
    std::pair<HWND, long long> best = {nullptr, 0};
    EnumWindows(FindWindow, reinterpret_cast<LPARAM>(&best));
    if (!best.first) return false;
    g_game = best.first;
    g_gameThread = GetWindowThreadProcessId(g_game, nullptr);
    g_messageHook = SetWindowsHookExW(WH_GETMESSAGE, GameMessages, g_module, g_gameThread);
    if (g_messageHook) Log(L"已接入游戏线程=%lu", g_gameThread);
    return g_messageHook != nullptr;
}
DWORD WINAPI Worker(void*) {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_module, path, _countof(path));
    std::wstring directory(path);
    directory.resize(directory.find_last_of(L"\\/"));
    g_directory = directory;
    g_ini = directory + L"\\OneKeyPotion.ini";
    g_log = directory + L"\\OneKeyPotion.log";
    if (!GetPrivateProfileIntW(L"General", L"Enabled", 0, g_ini.c_str())) {
        Log(L"插件已停用：Enabled=0"); return 0;
    }
    wchar_t guiKeyText[128]={};
    GetPrivateProfileStringW(L"General",L"GuiKey",L"F9",guiKeyText,128,g_ini.c_str());
    g_guiKey=potion_keys::Parse(guiKeyText);
    if(!potion_keys::KeyboardKey(g_guiKey)) {Log(L"无效 GuiKey，请设置键名或虚拟键码");return 0;}
    g_hotkey=DefaultDrinkKey();
    LoadCatalog();
    g_dispatch = RegisterWindowMessageW(L"OneKeyPotion.Tick.v1");
    bool validated = false;
    for (int attempt = 0; attempt < 120; ++attempt) {
        if (potion_native::Validate()) { validated = true; break; }
        Sleep(500);
    }
    if (!validated) { Log(L"客户端接口校验失败，补药已停用"); return 0; }
    Log(L"插件已载入，窗口快捷键 %s",potion_keys::Name(g_guiKey).c_str());
    Notice(L"一键喝药已载入，请打开配置窗口设置角色方案",true);
    g_ready = true;
    for (;;) {
        if (Attach() && !g_posted.exchange(true)) {
            if (!PostThreadMessageW(g_gameThread, g_dispatch, 0, 0)) g_posted = false;
        }
        Sleep(50);
    }
}
LONG g_started = 0;
}
extern "C" __declspec(dllexport) BOOL ClientPatchPluginInit() {
    if (InterlockedCompareExchange(&g_started, 1, 0)) return TRUE;
    HANDLE thread = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
    if (!thread) { InterlockedExchange(&g_started, 0); return FALSE; }
    CloseHandle(thread); return TRUE;
}
BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) { g_module = module; DisableThreadLibraryCalls(module); }
    return TRUE;
}
