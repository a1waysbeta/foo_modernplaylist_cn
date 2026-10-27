#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cwctype>
#endif
namespace modern_playlist {
inline constexpr unsigned search_delay_ms=500, incremental_idle_ms=1000;
struct search_field_definition { const wchar_t* label; const char* pattern; };
// Append entries to preserve saved field IDs. Path includes directory and filename.
inline constexpr search_field_definition search_fields[]={
    {L"所有字段",""}, {L"艺术家","[%artist%]"}, {L"标题","[%title%]"}, {L"专辑","[%album%]"},
    {L"流派","[%genre%]"}, {L"专辑艺术家","[%album artist%]"}, {L"注释","[%comment%]"}, {L"路径","%path%"}};
inline constexpr unsigned search_field_count=sizeof(search_fields)/sizeof(search_fields[0]);
inline constexpr const wchar_t* search_scopes[]={L"当前播放列表",L"媒体库"};
struct search_settings {
    bool visible=true;
    unsigned field=0, scope=0, typing_field=1; // 1..7: metadata; search_field_count: group key.
    uint32_t color=0x0066d9ff; // COLORREF: warm yellow
};
// Scope highlights to metadata supplied by the display format. Mixed-field
// formats are conservative: a scoped query must never mark unrelated metadata.
inline uint32_t search_format_fields(const std::string& pattern) {
    uint32_t fields=0;
    auto add=[&](std::string name) {
        for(auto& c:name) if(c>='A' && c<='Z') c+=char('a'-'A');
        const char* names[]={"","artist","title","album","genre","album artist","comment","path"};
        for(unsigned i=1;i<search_field_count;++i) if(name==names[i]) fields|=1U<<i;
        if(name=="filename" || name=="filename_ext" || name=="directoryname") fields|=1U<<7;
    };
    bool quoted=false;
    for(size_t i=0;i<pattern.size();++i) {
        if(pattern[i]=='\'') {quoted=!quoted;continue;}
        if(quoted) continue;
        if(pattern[i]=='/' && i+1<pattern.size() && pattern[i+1]=='/') {
            i=pattern.find_first_of("\r\n",i);if(i==std::string::npos)break;continue;
        }
        if(pattern[i]=='%') {
            const auto end=pattern.find('%',i+1);if(end==std::string::npos)break;
            add(pattern.substr(i+1,end-i-1));i=end;
        } else if(pattern[i]=='$') {
            const auto end=pattern.find('(',i+1);if(end==std::string::npos)continue;
            auto function=pattern.substr(i+1,end-i-1);
            for(auto& c:function)if(c>='A' && c<='Z')c+=char('a'-'A');
            if(function!="meta" && function!="meta_sep")continue;
            auto stop=pattern.find_first_of(",)",end+1);if(stop==std::string::npos)continue;
            auto name=pattern.substr(end+1,stop-end-1);
            const auto first=name.find_first_not_of(" '\t"),last=name.find_last_not_of(" '\t");
            if(first!=std::string::npos)add(name.substr(first,last-first+1));
        }
    }
    // The built-in title fallback is still the Title field, whose native title
    // formatting already substitutes the filename when the tag is absent.
    if(pattern=="$if2(%title%,%filename_ext%)") return 1U<<2;
    return fields;
}
inline bool search_highlight_field(uint32_t fields,unsigned selected) {
    return selected==0 || (selected<search_field_count && fields==(1U<<selected));
}
struct text_match { size_t start, length; };
inline bool equal_search_text(const wchar_t* a,const wchar_t* b,size_t length) {
#ifdef _WIN32
    return CompareStringOrdinal(a,int(length),b,int(length),TRUE)==CSTR_EQUAL;
#else
    for(size_t i=0;i<length;++i) if(std::towlower(a[i])!=std::towlower(b[i])) return false;
    return true;
#endif
}
inline std::vector<text_match> search_matches(const std::wstring& text,const std::vector<std::wstring>& terms) {
    std::vector<text_match> matches;
    for(size_t i=0;i<text.size();) {
        size_t length=0;
        for(const auto& term:terms) if(!term.empty() && term.size()<=text.size()-i &&
            equal_search_text(text.data()+i,term.data(),term.size())) length=std::max(length,term.size());
        if(length) { matches.push_back({i,length}); i+=length; } else ++i;
    }
    return matches;
}
inline bool search_prefix(const std::wstring& text,const std::wstring& prefix) {
    return !prefix.empty() && text.size()>=prefix.size() && equal_search_text(text.data(),prefix.data(),prefix.size());
}
// Only highlight literal free-text queries. The SDK owns the full query grammar;
// guessing positive operands in NOT/OR/title-format expressions is misleading.
inline std::vector<std::wstring> literal_search_terms(const std::wstring& query) {
    std::vector<std::wstring> terms;
    for(size_t i=0;i<query.size();) {
        if(query[i]==L' ' || query[i]==L'\t') { ++i; continue; }
        std::wstring word; bool quoted=query[i]==L'"';
        if(quoted) {
            ++i; while(i<query.size() && query[i]!=L'"') word+=query[i++];
            if(i==query.size()) return {}; ++i;
        } else {
            while(i<query.size() && query[i]!=L' ' && query[i]!=L'\t') word+=query[i++];
            if(word.find_first_of(L"()%$\"\r\n")!=std::wstring::npos) return {};
            for(const auto* op:{L"AND",L"OR",L"NOT",L"IS",L"HAS",L"GREATER",L"LESS",L"EQUAL",L"PRESENT",L"MISSING",L"BEFORE",L"AFTER",L"SINCE",L"DURING",L"ALL"})
                if(word==op) return {};
        }
        if(!word.empty()) terms.push_back(std::move(word));
    }
    return terms;
}
struct incremental_search {
    std::wstring text;
    uint64_t last_input=0;
    void clear() { text.clear(); last_input=0; }
    bool expired(uint64_t now) const { return !text.empty() && now-last_input>=incremental_idle_ms; }
    void input(wchar_t ch,uint64_t now) {
        if(expired(now)) clear();
        last_input=now;
        if(ch==L'\b') {
            if(!text.empty()) {
                const auto tail=text.back(); text.pop_back();
                if(tail>=0xdc00 && tail<=0xdfff && !text.empty() && text.back()>=0xd800 && text.back()<=0xdbff) text.pop_back();
            }
        } else if(ch>=L' ' && ch!=0x7f && text.size()<256) text+=ch;
    }
};
}
