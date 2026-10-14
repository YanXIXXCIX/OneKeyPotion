#pragma once
#include <windows.h>
#include <string>
#include <cwctype>
#include <cwchar>
namespace potion_keys {
inline bool KeyboardKey(int key) {return key>=VK_BACK && key<=0xFE;}
inline int Parse(std::wstring name) {
    const auto first=name.find_first_not_of(L" \t\r\n"),last=name.find_last_not_of(L" \t\r\n");
    if(first==std::wstring::npos) return 0;
    name=name.substr(first,last-first+1);
    for(auto& ch:name) ch=static_cast<wchar_t>(towupper(ch));
    if(name.size()==1) {
        const wchar_t ch=name[0];
        if((ch>=L'A' && ch<=L'Z') || (ch>=L'0' && ch<=L'9')) return ch;
        const SHORT scan=VkKeyScanW(ch);return scan==-1?0:(scan&0xFF);
    }
    wchar_t* end=nullptr;
    if(name[0]==L'F') {
        const long number=wcstol(name.c_str()+1,&end,10);
        if(*end==0 && number>=1 && number<=24) return VK_F1+static_cast<int>(number)-1;
    }
    struct Alias {const wchar_t* name;int key;};
    static const Alias aliases[]={
        {L"SPACE",VK_SPACE},{L"ENTER",VK_RETURN},{L"RETURN",VK_RETURN},{L"TAB",VK_TAB},
        {L"ESC",VK_ESCAPE},{L"ESCAPE",VK_ESCAPE},{L"BACKSPACE",VK_BACK},{L"INSERT",VK_INSERT},
        {L"DELETE",VK_DELETE},{L"HOME",VK_HOME},{L"END",VK_END},{L"PAGEUP",VK_PRIOR},{L"PAGEDOWN",VK_NEXT},
        {L"UP",VK_UP},{L"DOWN",VK_DOWN},{L"LEFT",VK_LEFT},{L"RIGHT",VK_RIGHT},
        {L"SHIFT",VK_SHIFT},{L"LSHIFT",VK_LSHIFT},{L"RSHIFT",VK_RSHIFT},
        {L"CTRL",VK_CONTROL},{L"CONTROL",VK_CONTROL},{L"LCTRL",VK_LCONTROL},{L"RCTRL",VK_RCONTROL},
        {L"ALT",VK_MENU},{L"LALT",VK_LMENU},{L"RALT",VK_RMENU},
        {L"LWIN",VK_LWIN},{L"RWIN",VK_RWIN},{L"MENU",VK_APPS},{L"CAPSLOCK",VK_CAPITAL},
        {L"NUMLOCK",VK_NUMLOCK},{L"SCROLLLOCK",VK_SCROLL},{L"PAUSE",VK_PAUSE},{L"PRINTSCREEN",VK_SNAPSHOT},
        {L"MULTIPLY",VK_MULTIPLY},{L"ADD",VK_ADD},{L"SUBTRACT",VK_SUBTRACT},{L"DECIMAL",VK_DECIMAL},{L"DIVIDE",VK_DIVIDE}
    };
    for(const auto& alias:aliases) if(name==alias.name) return alias.key;
    if(name.size()==7 && name.compare(0,6,L"NUMPAD")==0 && name[6]>=L'0' && name[6]<=L'9') return VK_NUMPAD0+name[6]-L'0';
    const wchar_t* numeric=nullptr;int base=10;
    if(name.compare(0,3,L"VK:")==0) numeric=name.c_str()+3;
    else if(name.compare(0,2,L"0X")==0) {numeric=name.c_str();base=16;}
    if(numeric) {const long key=wcstol(numeric,&end,base);if(end!=numeric && *end==0 && key>=VK_BACK && key<=0xFE) return static_cast<int>(key);}
    return 0;
}
inline int ResolvedKey(const MSG& message) {
    const int key=static_cast<int>(message.wParam);
    if(key==VK_SHIFT) return static_cast<int>(MapVirtualKeyW((message.lParam>>16)&0xFF,MAPVK_VSC_TO_VK_EX));
    if(key==VK_CONTROL) return (message.lParam&(1u<<24))?VK_RCONTROL:VK_LCONTROL;
    if(key==VK_MENU) return (message.lParam&(1u<<24))?VK_RMENU:VK_LMENU;
    return key;
}
inline bool Matches(int configured,const MSG& message) {return configured==static_cast<int>(message.wParam) || configured==ResolvedKey(message);}
inline bool ModifiersAllowed(int key,bool ctrl,bool shift,bool alt,bool win) {
    const bool ownCtrl=key==VK_CONTROL || key==VK_LCONTROL || key==VK_RCONTROL;
    const bool ownShift=key==VK_SHIFT || key==VK_LSHIFT || key==VK_RSHIFT;
    const bool ownAlt=key==VK_MENU || key==VK_LMENU || key==VK_RMENU;
    const bool ownWin=key==VK_LWIN || key==VK_RWIN;
    return (!ctrl || ownCtrl) && (!shift || ownShift) && (!alt || ownAlt) && (!win || ownWin);
}
inline std::wstring Name(int key) {
    UINT scan=MapVirtualKeyW(static_cast<UINT>(key),MAPVK_VK_TO_VSC);
    if(key==VK_RCONTROL || key==VK_RMENU || (key>=VK_PRIOR && key<=VK_DOWN) || key==VK_INSERT || key==VK_DELETE || key==VK_DIVIDE || key==VK_NUMLOCK) scan|=0x100;
    wchar_t name[64]={};
    if(GetKeyNameTextW(static_cast<LONG>(scan<<16),name,64)>0) return name;
    return L"VK:"+std::to_wstring(key);
}
}
