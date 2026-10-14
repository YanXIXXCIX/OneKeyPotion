#pragma once
#define NOMINMAX
#include <windows.h>
#include <cstdint>
#include <initializer_list>

namespace potion_native {
// Baseline matches S4A21ClientPatch EquipmentSwap. No A21-wide address scanner.
inline uintptr_t Address(uintptr_t preferred) {
    return reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) + preferred - 0x400000;
}
inline bool Bytes(uintptr_t address, std::initializer_list<unsigned char> expected) {
    __try {
        const auto* actual = reinterpret_cast<const unsigned char*>(Address(address));
        size_t i = 0;
        for (auto byte : expected) if (actual[i++] != byte) return false;
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool Validate() {
    return Bytes(0x1648AC0, {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x85,0xC0}) &&
        Bytes(0x1648BA0, {0x55,0x8B,0xEC,0x8B,0x45,0x08}) &&
        Bytes(0x16FA5F0, {0x55,0x8B,0xEC,0x6A,0xFF}) &&
        Bytes(0x1702E70, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0xE6,0xB3,0xDC,0x02}) &&
        Bytes(0xB1DA50, {0x55,0x8B,0xEC,0x8B,0x45,0x08,0x56,0x8B,0xF1}) &&
        Bytes(0xB1DA90, {0xC7,0x01,0x04,0x5B,0x16,0x03,0x83,0xC1,0x04}) &&
        Bytes(0x27C3660, {0x55,0x8B,0xEC,0x83,0xEC,0x10,0x56,0x8B,0xF1}) &&
        Bytes(0x16E5DC0, {0x55,0x8B,0xEC,0x6A,0xFF,0x68,0x4B,0xA0,0xDC,0x02});
}
inline uintptr_t Character() {
    __try { return *reinterpret_cast<uintptr_t*>(Address(0x3B3DFB8)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
inline bool TextInput() {
    __try { return *reinterpret_cast<unsigned char*>(Address(0x4C6D016)) != 0; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return true; }
}
inline unsigned int CharacterId() {
    if (!Character() || !Bytes(0x19E3290, {0x55,0x8B,0xEC,0x51,0x56,0x8B,0xF1,0x83,0x7E,0x54,0x00})) return 0;
    __try {
        auto* manager = *reinterpret_cast<void**>(Address(0x3A5C980));
        if (!manager) return 0;
        using GetUser = void* (__thiscall*)(void*);
        auto* user = reinterpret_cast<unsigned char*>(reinterpret_cast<GetUser>(Address(0x19E3290))(manager));
        return user ? *reinterpret_cast<unsigned short*>(user + 8) : 0;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return 0; }
}
// Copy only template IDs. All native reads stay on the game thread.
inline bool InventoryIds(int* ids, int capacity, int& size) {
    size = 0;
    __try {
        const auto list = *reinterpret_cast<uintptr_t*>(Address(0x3A5C9BC));
        if (!list) return false;
        const auto begin = *reinterpret_cast<uintptr_t*>(list + 0x58);
        const auto end = *reinterpret_cast<uintptr_t*>(list + 0x5C);
        if (end < begin || (end-begin)%4 || (end-begin)/4 > 1024) return false;
        for (int i=3; i<static_cast<int>((end-begin)/4); ++i) {
            const auto item = *reinterpret_cast<uintptr_t*>(begin+i*4);
            if (!item) continue;
            auto* table = *reinterpret_cast<uintptr_t**>(item);
            using Info = void* (__thiscall*)(void*);
            auto* info = reinterpret_cast<unsigned char*>(reinterpret_cast<Info>(table[10])(reinterpret_cast<void*>(item)));
            if (!info) continue;
            const int id = *reinterpret_cast<int*>(info+24);
            if (id>0 && size<capacity) ids[size++]=id;
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { size=0; return false; }
}
inline bool EffectRemaining(int itemId, int& milliseconds) {
    milliseconds = 0;
    __try {
        using Clock = unsigned int (__thiscall*)(void*);
        const auto now = reinterpret_cast<Clock>(Address(0x27C3660))(
            reinterpret_cast<void*>(Address(0x3A2A040)));
        // Client's timed drug effect list. Confirmed with item 2600656:
        // node+8=item ID, +12=duration ms, +16=deadline on the native clock.
        const auto head = *reinterpret_cast<uintptr_t*>(Address(0x3A8F190));
        if (!head) return false;
        auto node = *reinterpret_cast<uintptr_t*>(head);
        for (int visited = 0; node != head; ++visited) {
            if (!node || visited >= 256) return false;
            const int id = *reinterpret_cast<int*>(node + 8);
            const auto deadline = *reinterpret_cast<unsigned int*>(node + 16);
            if (id == itemId) {
                const int remaining = static_cast<int>(deadline - now);
                if (remaining > milliseconds) milliseconds = remaining;
            }
            node = *reinterpret_cast<uintptr_t*>(node);
        }
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool PotionEffectOriginMatches(uintptr_t effect, int itemId) {
    if(!effect || itemId<=0) return false;
    // Generic item-effect info tags its creator at GetInfo()+0x30 (=+0x34).
    // The native item-use initialization path 0x19FEB55..0x19FEC32 also
    // records the current waste item's template ID at +0xD0. It can be zero
    // for stat-only effects, but it is not an unrelated field.
    const int usedItem=*reinterpret_cast<const int*>(effect+0xD0);
    // The actual used item is stronger provenance than a cloned effect-info
    // tag. A composite potion must not impersonate its component medicines.
    if(usedItem>0) return usedItem==itemId;
    // Script appendage setter 0x19FC700 writes kind=5, template ID=+0xF0,
    // and source flag=1. Check its tags before interpreting that field.
    const int scriptItem=*reinterpret_cast<const int*>(effect+0xF0);
    if(*reinterpret_cast<const int*>(effect+0xD8)==5 &&
        *reinterpret_cast<const int*>(effect+0x108)==1 && scriptItem>0)
        return scriptItem==itemId;
    return *reinterpret_cast<const int*>(effect+0x34)==itemId;
}
// Second native effect source, for temporary stat appendages (e.g. capsules).
// Kept separate until its item families pass an in-game comparison.
inline bool ReadProtectedInt(uintptr_t field, int& value) {
    // Same four aligned checksum variants and fallback as the native readers.
    // Read only: malformed values never invoke the native tamper callback.
    value=0;
    __try {
        const auto registry=*reinterpret_cast<uintptr_t*>(Address(0x3B89618));
        if(!registry) return false;
        const auto index=*reinterpret_cast<const unsigned int*>(field);
        const auto block=*reinterpret_cast<const uintptr_t*>(registry+0x24+(index>>16)*4);
        if(!block) return false;
        const auto key=*reinterpret_cast<const unsigned int*>(block+0x2114+(index&0xFFFF)*4);
        const auto salt=key&0xFFFF;
        const auto plain=*reinterpret_cast<const unsigned int*>(field+4)^(salt|(salt<<16));
        const auto low=plain&0xFFFF, high=plain>>16;
        unsigned int checksum=0;
        switch(field&0xF) {
        case 0: checksum=((high-salt+low)^salt)&0xFFFF; break;
        case 4: checksum=((low-high)^salt)&0xFFFF; break;
        case 8: checksum=((high*low)^salt)&0xFFFF; break;
        case 12: checksum=((high+low+salt)^salt)&0xFFFF; break;
        default: checksum=((high+low)^salt)&0xFFFF; break;
        }
        if(checksum!=(key>>16)) return false;
        value=static_cast<int>(plain);return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool VariousBuffActive(int buffId, bool& active) {
    active=false;
    if(buffId<=0 || !Bytes(0xD037C0,{0x55,0x8B,0xEC,0x83,0xEC,0x18,0x53,0x56,0x57}) ||
        !Bytes(0xD0B393,{0xA1,0x30,0xF6,0x9A,0x03}) ||
        !Bytes(0xD08190,{0x55,0x8B,0xEC,0x83,0xEC,0x08,0x56})) return false;
    __try {
        // This singleton is created by the normal character-buff protocol/use
        // path. Do not instantiate it or send a request from a status read.
        const auto manager=*reinterpret_cast<uintptr_t*>(Address(0x39AF630));
        if(!manager) return true;
        using Find=uintptr_t (__thiscall*)(void*,int);
        const auto record=reinterpret_cast<Find>(Address(0xD037C0))(reinterpret_cast<void*>(manager),buffId);
        if(record) {
            if(*reinterpret_cast<const int*>(record)!=buffId) return false;
            // Native 0xD08870 uses record presence to block re-use of town
            // medicines. Counts/end-of-dungeon/expiry/removal stay native.
            active=true;
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool AppendageState(int itemId, bool& active, int& milliseconds) {
    active=false;
    milliseconds = 0;
    if (!Bytes(0x17B03B0,{0x8B,0x81,0xC4,0x28,0x00,0x00,0xC3}) ||
        !Bytes(0x19FC760,{0x8A,0x41,0x60,0xC3}) ||
        !Bytes(0x19FEC32,{0x89,0x9E,0xD0,0x00,0x00,0x00}) ||
        !Bytes(0x19FC700,{0x55,0x8B,0xEC,0x8B,0x45,0x08,0xC7,0x81,0xD8,0x00,0x00,0x00,0x05,0x00,0x00,0x00}) ||
        !Bytes(0x19FD4C0,{0x55,0x8B,0xEC,0x51,0x56,0x8B,0xF1,0x57,0x8D,0x4E,0x08})) return false;
    __try {
        const auto actor=Character();
        if(!actor) return false;
        const auto* actorTable=*reinterpret_cast<uintptr_t**>(actor);
        if(actorTable[0x6A0/4]!=Address(0x17B03B0)) return false;
        const auto manager=*reinterpret_cast<uintptr_t*>(actor+0x28C4);
        if(!manager) return false;
        const auto head=*reinterpret_cast<uintptr_t*>(manager+12);
        if(!head) return false;
        auto node=*reinterpret_cast<uintptr_t*>(head);
        for(int visited=0;node!=head;++visited) {
            if(!node || visited>=4096) return false;
            const auto effect=*reinterpret_cast<uintptr_t*>(node+12);
            if(effect && *reinterpret_cast<uintptr_t*>(effect+4)==actor &&
                PotionEffectOriginMatches(effect,itemId)) {
                const auto* table=*reinterpret_cast<uintptr_t**>(effect);
                const auto getter=table[0x8C/4];
                if(getter!=Address(0x19FC760) && getter!=Address(0xBE75C0)) return false;
                if(getter==Address(0xBE75C0) && !Bytes(0xBE75C0,{0x56,0x8B,0xF1,0xE8,0xF8,0x5E,0xE1,0x00})) return false;
                using Active=bool (__thiscall*)(void*);
                if(reinterpret_cast<Active>(getter)(reinterpret_cast<void*>(effect))) {
                    // Remaining duration uses a protected value at +0x2C.
                    // Native reader decodes it and subtracts the native timer.
                    using Remaining=int (__thiscall*)(void*);
                    const int left=reinterpret_cast<Remaining>(Address(0x19FD4C0))(reinterpret_cast<void*>(effect));
                    int deadline=0;
                    if(!ReadProtectedInt(effect+0x2C,deadline)) return false;
                    // A zero deadline is an untimed live state, not an expired timer.
                    if(left>0 || deadline==0) active=true;
                    if(left>milliseconds) milliseconds=left;
                }
            }
            node=*reinterpret_cast<uintptr_t*>(node);
        }
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool AppendageRemaining(int itemId, int& milliseconds) {
    bool active=false;return AppendageState(itemId,active,milliseconds);
}
inline bool FindItem(int itemId, int& slot, int& count) {
    slot = -1; count = 0;
    __try {
        const auto list = *reinterpret_cast<uintptr_t*>(Address(0x3A5C9BC));
        if (!list) return false;
        const auto begin = *reinterpret_cast<uintptr_t*>(list + 0x58);
        const auto end = *reinterpret_cast<uintptr_t*>(list + 0x5C);
        if (end < begin || (end - begin) % 4 || (end - begin) / 4 > 1024) return false;
        for (int i = 3; i < static_cast<int>((end - begin) / 4); ++i) {
            const auto item = *reinterpret_cast<uintptr_t*>(begin + i * 4);
            if (!item) continue;
            auto* table = *reinterpret_cast<uintptr_t**>(item);
            using Info = void* (__thiscall*)(void*);
            auto* info = reinterpret_cast<unsigned char*>(reinterpret_cast<Info>(table[10])(
                reinterpret_cast<void*>(item)));
            if (!info || *reinterpret_cast<int*>(info + 24) != itemId) continue;
            using Count = unsigned int (__thiscall*)(void*);
            const auto quantity = reinterpret_cast<Count>(table[18])(reinterpret_cast<void*>(item));
            if (!quantity || quantity > 0x7FFFFFFF) continue;
            slot = i; count = static_cast<int>(quantity); return true;
        }
        return true; // Valid inventory, configured item absent.
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool InClientImage(uintptr_t pointer, size_t bytes = 4) {
    const auto base=reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    const auto* dos=reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if(!base || dos->e_magic!=IMAGE_DOS_SIGNATURE) return false;
    const auto* nt=reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if(nt->Signature!=IMAGE_NT_SIGNATURE) return false;
    const auto size=static_cast<uintptr_t>(nt->OptionalHeader.SizeOfImage);
    return pointer>=base && pointer-base<size && bytes<=size-(pointer-base);
}
inline bool NormalWasteLineage(const uintptr_t* table) {
    __try {
        const auto vtable=reinterpret_cast<uintptr_t>(table);
        if(vtable<4 || !InClientImage(vtable-4,0x14C)) return false;
        // MSVC x86 CompleteObjectLocator and ClassHierarchyDescriptor.
        // 0x396A8B4 is the shared native waste/medicine base descriptor,
        // confirmed in both old handlers and their derived vtables. This is
        // narrower than the generic stackable parent (0x395FC28).
        const auto locator=table[-1];
        if(!InClientImage(locator,20)) return false;
        const auto* col=reinterpret_cast<const unsigned int*>(locator);
        if(col[0]!=0 || col[1]!=0) return false;
        const auto hierarchy=static_cast<uintptr_t>(col[4]);
        if(!InClientImage(hierarchy,16)) return false;
        const auto* ch=reinterpret_cast<const unsigned int*>(hierarchy);
        const auto count=ch[2];
        if(ch[0]!=0 || count==0 || count>64) return false;
        const auto array=static_cast<uintptr_t>(ch[3]);
        if(!InClientImage(array,static_cast<size_t>(count)*4)) return false;
        const auto* classes=reinterpret_cast<const unsigned int*>(array);
        for(unsigned int i=0;i<count;++i) {
            const auto descriptor=static_cast<uintptr_t>(classes[i]);
            if(!InClientImage(descriptor,24)) return false;
            const auto* base=reinterpret_cast<const unsigned int*>(descriptor);
            if(base[0]==Address(0x396A8B4)) {
                // Retain the ordinary item-info getter and native waste-info
                // getter as independent family fingerprints.
                return table[0x28/4]==Address(0x11BA6D0) &&
                    table[0x148/4]==Address(0x16DD0A0) &&
                    InClientImage(table[0x140/4]);
            }
        }
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool CheckPotionUse(int slot, int expectedId, uintptr_t* observedTable=nullptr,
    uintptr_t* observedHandler=nullptr) {
    if(observedTable) *observedTable=0;
    if(observedHandler) *observedHandler=0;
    __try {
        const auto list=*reinterpret_cast<uintptr_t*>(Address(0x3A5C9BC));
        if(!list || slot<3 || expectedId<=0) return false;
        const auto begin=*reinterpret_cast<uintptr_t*>(list+0x58);
        const auto end=*reinterpret_cast<uintptr_t*>(list+0x5C);
        if(end<begin || (end-begin)%4 || (end-begin)/4>1024 || slot>=static_cast<int>((end-begin)/4)) return false;
        const auto item=*reinterpret_cast<uintptr_t*>(begin+slot*4);
        if(!item) return false;
        const auto* table=*reinterpret_cast<uintptr_t**>(item);
        if(observedTable) *observedTable=reinterpret_cast<uintptr_t>(table);
        if(!InClientImage(reinterpret_cast<uintptr_t>(table),0x14C)) return false;
        if(observedHandler) *observedHandler=table[0x140/4];
        // Derived native waste classes override use; matching just two base
        // handler addresses wrongly rejected legitimate medicines. Require
        // verified native lineage instead, before calling any native getter.
        if(!NormalWasteLineage(table)) return false;
        using Info=void* (__thiscall*)(void*);
        const auto* info=reinterpret_cast<unsigned char*>(reinterpret_cast<Info>(table[10])(reinterpret_cast<void*>(item)));
        return info && *reinterpret_cast<const int*>(info+24)==expectedId;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool PotionCoolingDown(int slot,int expectedId,bool& cooling) {
    cooling=false;
    if(!CheckPotionUse(slot,expectedId) ||
        !Bytes(0xEE7130,{0x8B,0x01,0x8B,0x90,0xFC,0x00,0x00,0x00}) ||
        !Bytes(0xEE71B0,{0x8B,0x01,0x8B,0x90,0xF4,0x00,0x00,0x00}) ||
        !Bytes(0x16E0F50,{0x55,0x8B,0xEC,0x83,0xEC,0x1C,0x56})) return false;
    __try {
        const auto list=*reinterpret_cast<uintptr_t*>(Address(0x3A5C9BC));
        const auto begin=*reinterpret_cast<uintptr_t*>(list+0x58);
        const auto item=*reinterpret_cast<uintptr_t*>(begin+slot*4);
        const auto* table=*reinterpret_cast<const uintptr_t**>(item);
        if(table[0x9C/4]!=Address(0xEE7130) || table[0xFC/4]!=Address(0xEE71B0) ||
            table[0xF4/4]!=Address(0x16E0F50)) return false;
        using Cooling=bool (__thiscall*)(void*);
        cooling=reinterpret_cast<Cooling>(table[0x9C/4])(reinterpret_cast<void*>(item));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
inline bool UseItem(int slot, int expectedId) {
    if(!CheckPotionUse(slot,expectedId)) return false;
    __try {
        auto* list = *reinterpret_cast<void**>(Address(0x3A5C9BC));
        if (!list || slot < 3) return false;
        // The native caller uses an 8-byte registered/protected slot value.
        // Construct and destroy it with the original routines, then enter
        // the normal item-use path (including native restrictions/cooldown).
        unsigned int protectedSlot[2] = {};
        using Construct = void* (__thiscall*)(void*, const int*);
        using Destroy = void (__thiscall*)(void*);
        using Use = bool (__thiscall*)(void*, void*);
        reinterpret_cast<Construct>(Address(0xB1DA50))(protectedSlot, &slot);
        bool accepted = false;
        __try { accepted = reinterpret_cast<Use>(Address(0x1702E70))(list, protectedSlot); }
        __finally { reinterpret_cast<Destroy>(Address(0xB1DA90))(protectedSlot); }
        return accepted;
    } __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}
}
