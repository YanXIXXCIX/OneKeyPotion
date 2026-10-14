#pragma once
#include <windows.h>
#include <array>
#include <string>
// Native window/control bridge adapted from RewIO EquipmentSwap at
// 1402707de1a4d52a8d42a2a5fe330f0227a6d015. See source attribution.txt.
namespace potion_ui {
constexpr int kSlotCount = 64;
enum class Command { Button };
using CommandHandler = void(*)(Command, int);
struct UiState {
    std::wstring currentHotkey, statusText, schemeText, bagText, selectionText;
    bool hotkeyPending = false;
    int schemeFirst = 1;
    std::array<int, kSlotCount> itemIds = {};
};
bool Install(const wchar_t*, int, CommandHandler);
void Uninstall();
void* __cdecl WindowFactory(int, void*, void*);
void Toggle();
void Close();
bool IsOpen();
void Poll();
void Refresh(const UiState&);
}
