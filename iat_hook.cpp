#include "iat_hook.h"
#include "sdk.h"
#include <winnt.h>

namespace sdk { namespace iat {

void* patch(HMODULE target, const char* dll_name, const char* fn_name, void* replacement) {
    auto base = reinterpret_cast<BYTE*>(target);
    auto dos  = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return nullptr;
    auto nt   = reinterpret_cast<IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return nullptr;

    auto& imp_dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!imp_dir.Size) return nullptr;
    auto desc = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + imp_dir.VirtualAddress);

    for (; desc->Name; desc++) {
        const char* this_dll = reinterpret_cast<const char*>(base + desc->Name);
        if (_stricmp(this_dll, dll_name) != 0) continue;

        // OriginalFirstThunk holds the import name table (resilient).
        // FirstThunk holds the IAT we want to overwrite.
        // Both are parallel arrays; if OFT is missing fall back to FT for names.
        auto int_thunks = reinterpret_cast<IMAGE_THUNK_DATA*>(
            base + (desc->OriginalFirstThunk ? desc->OriginalFirstThunk : desc->FirstThunk));
        auto iat_thunks = reinterpret_cast<IMAGE_THUNK_DATA*>(base + desc->FirstThunk);

        for (int i = 0; int_thunks[i].u1.AddressOfData; i++) {
            // Skip ordinal-only imports — we match by name.
            if (IMAGE_SNAP_BY_ORDINAL(int_thunks[i].u1.Ordinal)) continue;
            auto* by_name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base + int_thunks[i].u1.AddressOfData);
            if (strcmp(reinterpret_cast<const char*>(by_name->Name), fn_name) != 0) continue;

            void** slot = reinterpret_cast<void**>(&iat_thunks[i].u1.Function);
            void* prev  = *slot;

            DWORD old_prot = 0;
            if (!VirtualProtect(slot, sizeof(void*), PAGE_READWRITE, &old_prot)) {
                sdk_log("[iat] VirtualProtect failed for %s!%s (err=%lu)", dll_name, fn_name, GetLastError());
                return nullptr;
            }
            *slot = replacement;
            VirtualProtect(slot, sizeof(void*), old_prot, &old_prot);
            sdk_log("[iat] patched %s!%s  slot=%p  old=%p  new=%p",
                    dll_name, fn_name, slot, prev, replacement);
            return prev;
        }
    }
    sdk_log("[iat] could not find import: %s!%s", dll_name, fn_name);
    return nullptr;
}

}} // namespace sdk::iat
