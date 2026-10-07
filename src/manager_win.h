#pragma once
#include <windows.h>
#include <shlwapi.h>
#include <shlobj.h>
#include <string>

namespace modern_playlist {
// Read the Explorer policy once per sort, so every comparison in that sort uses
// the same rule. The native functions supply Windows' language/symbol ordering.
struct explorer_name_compare {
    bool logical;
    explicit explorer_name_compare(bool use_logical=SHRestricted(REST_NOSTRCMPLOGICAL)==0)
        : logical(use_logical) {}
    int operator()(const std::wstring& a,const std::wstring& b) const {
        return logical?StrCmpLogicalW(a.c_str(),b.c_str()):StrCmpIW(a.c_str(),b.c_str());
    }
};
}
