// IAT (Import Address Table) hook.
//
// Walk a module's import directory, find a named function from a specific DLL,
// atomically replace the IAT slot with our pointer. Returns the previous pointer
// (so the hook function can chain) or nullptr if the slot wasn't found.
//
// Works only for functions the target module imports by name through its IAT.
// For Sacred.exe's CreateWindowExA / ChangeDisplaySettingsA / DirectDrawCreate*
// this is exactly the case (verified via pefile).
#pragma once
#include <windows.h>

namespace sdk { namespace iat {

void* patch(HMODULE target, const char* dll_name, const char* fn_name, void* replacement);

}} // namespace sdk::iat
