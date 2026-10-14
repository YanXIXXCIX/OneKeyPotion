// Included inside the plugin namespace; every entry runs on the game thread.
struct CatalogEntry { std::wstring name; bool usable = false; int mode = 0; std::vector<int> variousBuffs; bool variousOnly=false; };
std::map<int, CatalogEntry> g_catalog;
std::map<int,int> g_effectGroups;
std::map<int,std::vector<int>> g_effectGroupMembers;
std::map<int,std::vector<int>> g_effectGroupBuffs;
bool ConfirmedMutualExclusionGroup(int group) {
    // Only explicit variousbufflist "overlab ban group" values are exclusion
    // evidence. PVF appendage groups alone do not prove that drugs can't stack.
    return group>100000 && group<200000;
}
std::vector<int> g_savedSlots, g_slots, g_bag;
unsigned int g_characterId = 0;
int g_schemePage = 0, g_bagPage = 0, g_selected = -1, g_editKey = VK_F8;
bool g_dirty = false, g_recordKey = false, g_uiAttempted = false, g_uiReady = false;
std::wstring g_status = L"点击右侧药剂下方 + 加入方案";
constexpr const wchar_t* kLayout = L"Interface/OneKeyPotion.xui";
constexpr const wchar_t* kOwner = L"OneKeyPotion";
void UpdateUi();

#include "BuiltinPotions.h"
void LoadCatalog() {
    for(const auto& row:kBuiltinPotions) {
        const std::wstring name=*row.name?row.name:L"物品 "+std::to_wstring(row.id);
        g_catalog[row.id]={name,row.mode>=1 && row.mode<=4,row.mode,{row.buffs.begin(),row.buffs.end()},row.variousOnly};
        if(row.group>0 && row.mode!=3 && row.mode>0) {
            g_effectGroups[row.id]=row.group;g_effectGroupMembers[row.group].push_back(row.id);
            auto& buffs=g_effectGroupBuffs[row.group];
            for(int id:row.buffs) if(std::find(buffs.begin(),buffs.end(),id)==buffs.end()) buffs.push_back(id);
        }
    }
}
std::wstring ProfilePath() {
    return g_directory + L"\\OneKeyPotionData\\" + std::to_wstring(g_characterId) + L".ini";
}
void RebuildPotions() {
    g_potions.clear();
    g_unsupportedNames.clear();
    for (int id:g_savedSlots) {
        const auto entry=g_catalog.find(id);
        if (!id || entry==g_catalog.end()) continue;
        if (!entry->second.usable) {
            g_unsupportedNames.push_back(entry->second.name);
            Log(L"已保存但暂未接入自动补药：%s (%d)",entry->second.name.c_str(),id);
            continue;
        }
        Potion potion; potion.itemId=id; potion.name=entry->second.name; potion.effects.push_back(id);
        potion.appendage=entry->second.mode==2; potion.oneShot=entry->second.mode==3; potion.bothSources=!potion.oneShot;
        potion.variousOnly=entry->second.variousOnly;potion.variousBuffs=entry->second.variousBuffs;
        const auto group=g_effectGroups.find(id);
        if(!potion.oneShot && group!=g_effectGroups.end() && ConfirmedMutualExclusionGroup(group->second)) {
            potion.sharedGroup=group->second;potion.groupEffects=g_effectGroupMembers.at(group->second);
            potion.groupBuffs=g_effectGroupBuffs.at(group->second);
        }
        g_potions.push_back(std::move(potion));
    }
}
void RefreshBag() {
    int ids[1024]={}, count=0;
    if (!potion_native::InventoryIds(ids,_countof(ids),count)) {
        g_bag.clear(); g_status=L"背包读取失败，稍后点击刷新"; return;
    }
    g_bag.clear();
    for (int i=0;i<count;++i)
        if (g_catalog.count(ids[i]) && std::find(g_bag.begin(),g_bag.end(),ids[i])==g_bag.end()) g_bag.push_back(ids[i]);
    // Eligible medicines first. Stable inventory order within each group.
    std::stable_partition(g_bag.begin(),g_bag.end(),[](int id){return g_catalog.at(id).usable;});
    g_bagPage=(std::min)(g_bagPage,static_cast<int>(g_bag.empty()?0:(g_bag.size()-1)/32));
}
void LoadCharacter(unsigned int id) {
    potion_ui::Close();
    g_characterId=id; g_recordKey=false; g_keyHeld=false; g_selected=-1; g_schemePage=0; g_bagPage=0;
    g_slots.assign(64,0); g_savedSlots=g_slots; g_bag.clear(); g_dirty=false;
    if (!id) { g_potions.clear();g_unsupportedNames.clear(); return; }
    const auto file=ProfilePath();
    const bool exists=GetFileAttributesW(file.c_str())!=INVALID_FILE_ATTRIBUTES;
    if (exists) {
        const int schema=GetPrivateProfileIntW(L"Scheme",L"Schema",0,file.c_str());
        const int owner=GetPrivateProfileIntW(L"Scheme",L"CharacterId",0,file.c_str());
        const int capacity=GetPrivateProfileIntW(L"Scheme",L"Capacity",0,file.c_str());
        if (schema!=1 || owner!=static_cast<int>(id) || capacity<64 || capacity>4096 || capacity%8) {
            g_status=L"方案文件格式错误：未载入，请先检查文件";
            Notice(g_status.c_str()); RebuildPotions(); UpdateUi(); return;
        }
        g_slots.assign(static_cast<size_t>(capacity),0);
        const int key=GetPrivateProfileIntW(L"Scheme",L"Hotkey",DefaultDrinkKey(),file.c_str());
        g_hotkey=IsDrinkKey(key)?key:DefaultDrinkKey();
        for (int i=0;i<capacity;++i) {
            const auto field=L"Slot"+std::to_wstring(i);
            const int item=GetPrivateProfileIntW(L"Scheme",field.c_str(),0,file.c_str());
            if (item>0 && g_catalog.count(item) && std::find(g_slots.begin(),g_slots.end(),item)==g_slots.end()) g_slots[i]=item;
        }
        g_status=L"已载入本角色方案；修改后点击保存";
        if(key==g_guiKey) {g_dirty=true;g_status+=L"；喝药键与窗口键冲突，已采用备用键，请保存";}
    } else {
        g_hotkey=DefaultDrinkKey();
        g_status=L"本角色暂无方案，请从右侧添加药剂";
    }
    g_editKey=g_hotkey; g_savedSlots=g_slots; RebuildPotions(); RefreshBag(); UpdateUi();
    Log(L"角色方案载入：角色=%u，容量=%zu，可用药剂=%zu",id,g_slots.size(),g_potions.size());
}
bool SaveScheme() {
    if (!g_characterId) return false;
    const auto directory=g_directory+L"\\OneKeyPotionData";
    if (!CreateDirectoryW(directory.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return false;
    std::wstring text=L"\xFEFF[Scheme]\r\nSchema=1\r\nCharacterId="+std::to_wstring(g_characterId)+
        L"\r\nCapacity="+std::to_wstring(g_slots.size())+L"\r\nHotkey="+std::to_wstring(g_editKey)+L"\r\n";
    for(size_t i=0;i<g_slots.size();++i) text+=L"Slot"+std::to_wstring(i)+L"="+std::to_wstring(g_slots[i])+L"\r\n";
    const auto path=ProfilePath(), temporary=path+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE) return false;
    const DWORD size=static_cast<DWORD>(text.size()*sizeof(wchar_t)); DWORD written=0;
    bool ok=WriteFile(file,text.data(),size,&written,nullptr) && written==size && FlushFileBuffers(file);
    CloseHandle(file);
    if(ok) ok=MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=FALSE;
    if(!ok) { DeleteFileW(temporary.c_str()); return false; }
    g_savedSlots=g_slots; g_hotkey=g_editKey; g_keyHeld=false; g_dirty=false; RebuildPotions();
    Log(L"角色方案已保存：角色=%u，容量=%zu，可用药剂=%zu",g_characterId,g_slots.size(),g_potions.size());
    return true;
}
void UpdateUi() {
    potion_ui::UiState state;
    state.currentHotkey=g_recordKey?L"请按 F1-F24（"+potion_keys::Name(g_guiKey)+L" 为窗口键；Esc 取消）":L"补药 F"+std::to_wstring(g_editKey-VK_F1+1)+L"    "+potion_keys::Name(g_guiKey)+L" 打开/关闭配置";
    state.hotkeyPending=g_recordKey;
    state.statusText=std::wstring(g_dirty?L"[未保存] ":L"")+g_status;
    const auto occupied=std::count_if(g_slots.begin(),g_slots.end(),[](int id){return id>0;});
    state.schemeText=L"方案 "+std::to_wstring(occupied)+L" / "+std::to_wstring(g_slots.size())+L"   页 "+std::to_wstring(g_schemePage+1)+L" / "+std::to_wstring((std::max)(size_t(1),(g_slots.size()+31)/32));
    state.bagText=L"背包药剂 "+std::to_wstring(g_bag.size())+L"   页 "+std::to_wstring(g_bagPage+1)+L" / "+std::to_wstring((std::max)(size_t(1),(g_bag.size()+31)/32));
    state.selectionText=L"点 + 加入；选方案格后点另一格换位，或点删除";
    if(g_selected>=0 && static_cast<size_t>(g_selected)<g_slots.size()) {
        const int id=g_slots[g_selected]; const auto entry=g_catalog.find(id);
        if(entry!=g_catalog.end()) state.selectionText=L"选中："+entry->second.name+(entry->second.mode==3?L"（一次性：每次按键尝试一次，不检测药效）":(entry->second.usable?L"（持续药效补齐）":L"（暂未接入：跳过）"));
    }
    state.schemeFirst=g_schemePage*32+1;
    for(int i=0;i<32;++i) {
        const auto slot=static_cast<size_t>(g_schemePage*32+i);
        if(slot<g_slots.size()) state.itemIds[i]=g_slots[slot];
        const auto bag=static_cast<size_t>(g_bagPage*32+i);
        if(bag<g_bag.size()) state.itemIds[32+i]=g_bag[bag];
    }
    potion_ui::Refresh(state);
}
void UiCommand(potion_ui::Command,int control) {
    if(!g_characterId || g_job.active) { Notice(L"请等待补药结束后再编辑方案"); return; }
    if(control>=600 && control<632) control-=100; // Number-label clicks select the same slot.
    if(control>=532 && control<564) {
        const auto index=static_cast<size_t>(g_bagPage*32+control-532);
        if(index>=g_bag.size()) return;
        const int id=g_bag[index];
        const auto found=std::find(g_slots.begin(),g_slots.end(),id);
        if(found!=g_slots.end()) { g_selected=static_cast<int>(found-g_slots.begin()); g_schemePage=g_selected/32; g_status=L"已在方案中，不重复添加"; }
        else {
            auto empty=std::find(g_slots.begin(),g_slots.end(),0);
            const size_t target=static_cast<size_t>(empty-g_slots.begin());
            if(empty==g_slots.end()) {
                if(g_slots.size()>=4096) { g_status=L"方案已达到容量上限"; UpdateUi(); return; }
                g_slots.resize(g_slots.size()+8,0);
            }
            g_slots[target]=id; g_selected=static_cast<int>(target); g_dirty=true;
            g_schemePage=g_selected/32;
            g_status=g_catalog.at(id).mode==3?L"一次性药：每次按键尝试一次；点击保存生效":(g_catalog.at(id).usable?L"持续药：药效有效时跳过；点击保存生效":L"此药暂未接入，保存后仍跳过");
        }
    } else if(control>=500 && control<532) {
        const int index=g_schemePage*32+control-500;
        if(static_cast<size_t>(index)>=g_slots.size()) return;
        if(g_selected>=0 && g_selected!=index) {
            std::swap(g_slots[g_selected],g_slots[index]); g_selected=index; g_dirty=true; g_status=L"已换位；点击保存生效";
        } else g_selected=(g_selected==index)?-1:index;
    } else switch(control) {
        case 202:g_status=SaveScheme()?L"本角色方案已保存":L"保存失败，原方案保留"; break;
        case 203:Start(); break;
        case 204:std::fill(g_slots.begin(),g_slots.end(),0);g_selected=-1;g_dirty=true;g_status=L"已清空草稿，保存后生效";break;
        case 205:g_recordKey=true;g_status=L"按新的功能键，然后保存方案";break;
        case 206:g_recordKey=false;break;
        case 207:if(g_selected>=0) {g_slots[g_selected]=0;g_selected=-1;g_dirty=true;g_status=L"已删除；点击保存生效";}break;
        case 208:g_schemePage=(std::max)(0,g_schemePage-1);break;
        case 209:g_schemePage=(std::min)(static_cast<int>((g_slots.size()-1)/32),g_schemePage+1);break;
        case 210:g_bagPage=(std::max)(0,g_bagPage-1);break;
        case 211:g_bagPage=(std::min)(static_cast<int>(g_bag.empty()?0:(g_bag.size()-1)/32),g_bagPage+1);break;
        case 212:RefreshBag();break;
        default:return;
    }
    UpdateUi();
}
void InitUi() {
    if(g_uiAttempted) return;
    if(!g_nativeBound) g_nativeBound=ClientPatchBindGameNative(&g_native);
    if(!g_nativeBound) return;
    g_uiAttempted=true;
    if(!g_native.mount || !g_native.unmount || !g_native.reserveWindowIds || !g_native.releaseWindowIds ||
        !g_native.registerWindowFactory || !g_native.unregisterWindowFactory) {Log(L"原生窗口 API 不完整");return;}
    const auto path=g_directory+L"\\OneKeyPotion.xui";
    if(!g_native.mount(kLayout,path.c_str(),CLIENT_PATCH_GAME_NATIVE_MOUNT_STRICT)) {Log(L"XUI 挂载失败");return;}
    int id=-1;
    if(!g_native.reserveWindowIds(kOwner,1,CLIENT_PATCH_WINDOW_ID_EQUIPMENT,&id) ||
        !potion_ui::Install(kLayout,id,UiCommand) || !g_native.registerWindowFactory(kOwner,potion_ui::WindowFactory,nullptr)) {
        potion_ui::Uninstall();g_native.releaseWindowIds(kOwner);g_native.unmount(kLayout);Log(L"原生窗口接口校验或登记失败");return;
    }
    g_uiReady=true;Log(L"药剂窗口已登记，窗口快捷键 %s",potion_keys::Name(g_guiKey).c_str());
    const auto notice=L"药剂配置已载入："+potion_keys::Name(g_guiKey)+L" 打开窗口，保存后用配置快捷键喝药";
    Notice(notice.c_str());
}
void GuiTick() {
    InitUi();
    const auto id=potion_native::CharacterId();
    if(id!=g_characterId) { g_job.active=false;g_unconfirmed.clear();LoadCharacter(id); }
    static DWORD recoveryNext=0;
    if(id && !g_job.active && !g_unconfirmed.empty() && static_cast<int>(GetTickCount()-recoveryNext)>=0) {
        recoveryNext=GetTickCount()+250;RecoverUnconfirmed();
    }
    potion_ui::Poll();
    if(!Foreground()) {
        g_keyHeld=false;
        if(g_recordKey) {g_recordKey=false;UpdateUi();}
    }
}
bool GuiKey(MSG* message) {
    const bool snapshotRelease=g_guiKey==VK_SNAPSHOT && message->message==WM_KEYUP && message->wParam==VK_SNAPSHOT;
    if((message->message!=WM_KEYDOWN && message->message!=WM_SYSKEYDOWN && !snapshotRelease) || !CanAct()) return false;
    const int key=static_cast<int>(message->wParam);
    if(g_recordKey && potion_ui::IsOpen()) {
        if(message->lParam&(1u<<30)) return true;
        if(key==VK_ESCAPE) {g_recordKey=false;g_status=L"已取消录键";}
        else if(IsDrinkKey(key) && !(GetKeyState(VK_CONTROL)&0x8000) && !(GetKeyState(VK_MENU)&0x8000) && !(GetKeyState(VK_SHIFT)&0x8000)) {
            g_editKey=key;g_recordKey=false;g_dirty=true;g_status=L"新快捷键等待保存";
        }
        UpdateUi();return true;
    }
    if(potion_keys::Matches(g_guiKey,*message) && potion_keys::ModifiersAllowed(g_guiKey,
        (GetKeyState(VK_CONTROL)&0x8000)!=0,(GetKeyState(VK_SHIFT)&0x8000)!=0,
        (GetKeyState(VK_MENU)&0x8000)!=0,((GetKeyState(VK_LWIN)|GetKeyState(VK_RWIN))&0x8000)!=0)) {
        if((g_guiKey!=VK_SNAPSHOT || snapshotRelease) && (snapshotRelease || !(message->lParam&(1u<<30)))) {
            if(g_uiReady && g_characterId) {RefreshBag();UpdateUi();potion_ui::Toggle();}
            else Notice(L"药剂窗口未就绪，请查看 OneKeyPotion.log");
        }
        return true;
    }
    return false;
}
