#pragma once
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

namespace modern_playlist {
enum class special_column { none, mood, rating };
// Encode only markup written in a display expression. Metadata, quoted angle
// brackets and $char(60)/$char(62) remain literal when the result is rendered.
// Keep the saved expression and the independent sort expression untouched.
inline std::string column_display_pattern(const std::string& pattern) {
    std::string out; out.reserve(pattern.size());
    bool quoted=false;
    for(size_t i=0;i<pattern.size();++i) {
        const char c=pattern[i];
        if(c=='\'') { quoted=!quoted; out+=c; }
        else if(!quoted && c=='/' && i+1<pattern.size() && pattern[i+1]=='/') {
            const auto end=pattern.find_first_of("\r\n",i);
            if(end==std::string::npos) { out.append(pattern,i,std::string::npos); break; }
            out.append(pattern,i,end-i); i=end-1;
        } else if(!quoted && c=='%') {
            const auto end=pattern.find('%',i+1);
            if(end==std::string::npos) { out.append(pattern,i,std::string::npos); break; }
            out.append(pattern,i,end-i+1); i=end;
        } else if(!quoted && (c=='<' || c=='>')) out+=c=='<'?"$char(1)":"$char(2)";
        else out+=c;
    }
    return out;
}
struct color_run {
    size_t start, length;
    uint32_t color;
    int level=0; // Negative: dim toward background; positive: toward highlight.
    bool explicit_color=true;
};
struct colored_text { std::wstring text; std::vector<color_run> runs; };
// Resolve relative colors at paint time, using the current row/selection/theme.
inline uint32_t resolve_color(const color_run& run,uint32_t foreground,uint32_t background,uint32_t highlight) {
    const uint32_t base=run.explicit_color?run.color:foreground;
    const int level=std::clamp(run.level,-3,3);
    const unsigned amount=unsigned(level<0?-level:level);
    const uint32_t target=level<0?background:highlight;
    uint32_t result=0;
    for(unsigned shift:{0U,8U,16U}) {
        const auto from=(base>>shift)&255, to=(target>>shift)&255;
        result|=((from*(4-amount)+to*amount+2)/4)<<shift;
    }
    return result;
}
// Inline RGB escapes use foobar's COLORREF (BBGGRR) hex convention. Relative
// markers are emitted by column_display_pattern, never inferred from metadata.
inline colored_text parse_colors(const std::wstring& input) {
    colored_text out;
    uint32_t color=0; bool active=false;
    std::ptrdiff_t level=0;
    for(size_t i=0;i<input.size();) {
        if(input[i]==1 || input[i]==2) { level+=input[i]==1?-1:1; ++i; continue; }
        if(input[i]==3) {
            const auto end=input.find(wchar_t(3),i+1);
            if(end!=std::wstring::npos) {
                uint32_t value=0; bool valid=end-i==7;
                for(size_t j=i+1;valid && j<end;++j) {
                    const auto c=input[j]; int n=c>=L'0' && c<=L'9'?c-L'0':c>=L'a' && c<=L'f'?c-L'a'+10:c>=L'A' && c<=L'F'?c-L'A'+10:-1;
                    if(n<0) valid=false; else value=value*16+unsigned(n);
                }
                if(valid || end==i+1) { active=valid; color=value; i=end+1; continue; }
            }
            ++i; continue; // Never show a control character, even for malformed input.
        }
        const size_t at=out.text.size(); out.text+=input[i++];
        if(active || level) {
            const int shade=int(std::clamp(level,std::ptrdiff_t(-3),std::ptrdiff_t(3)));
            if(!out.runs.empty() && out.runs.back().color==color && out.runs.back().level==shade &&
               out.runs.back().explicit_color==active && out.runs.back().start+out.runs.back().length==at) ++out.runs.back().length;
            else out.runs.push_back({at,1,color,shade,active});
        }
    }
    return out;
}
inline int rating_value(const std::wstring& text) {
    const auto first=text.find_first_not_of(L" \t\r\n");
    if(first==std::wstring::npos || text[first]<L'1' || text[first]>L'5') return 0;
    return text.find_first_not_of(L" \t\r\n",first+1)==std::wstring::npos?int(text[first]-L'0'):0;
}
inline bool mood_value(const std::wstring& text) {
    return !text.empty() && text!=L"0" && text!=L"?";
}
// A context-menu command and the group it is listed in.
struct menu_command_name { std::string group, name; };
// foo_playcount's Rating group holds commands named 1 to 5; digits stay the
// same in translated menus. The group's one other command clears the rating.
// Returns the command for a value of 0-5, or -1 when no target is certain.
inline int rating_menu_target(const std::vector<menu_command_name>& commands,int value) {
    if(value<0 || value>5) return -1;
    auto plain=[](std::string name) { name.erase(std::remove(name.begin(),name.end(),'&'),name.end()); return name; };
    std::vector<std::string> groups;
    for(const auto& command:commands)
        if(std::find(groups.begin(),groups.end(),command.group)==groups.end()) groups.push_back(command.group);
    for(const auto& group:groups) {
        int digits[6]={-1,-1,-1,-1,-1,-1}; std::vector<int> others;
        for(size_t i=0;i<commands.size();++i) {
            if(commands[i].group!=group) continue;
            const auto name=plain(commands[i].name);
            if(name.size()==1 && name[0]>='1' && name[0]<='5') digits[name[0]-'0']=int(i); else others.push_back(int(i));
        }
        if(std::any_of(digits+1,digits+6,[](int i) { return i<0; })) continue;
        if(value) return digits[value];
        if(others.size()==1) return others.front();
        for(int i:others) if(plain(commands[size_t(i)].name)=="<未设置>") return i;
        return -1;
    }
    return -1;
}
// Shared by column sizing, drawing and hit testing. The star path occupies 5/6
// of its bitmap width; compact slots retain a small gap between visible tips.
struct rating_cell_metrics { int icon_size, pitch, minimum_width; };
inline rating_cell_metrics rating_cells(int icon_size,int row_height,int default_pitch,int padding,int gap,bool compact) {
    const int size=std::clamp(std::min(icon_size,row_height),1,256);
    const int pitch=compact?std::max(1,(size*5+5)/6+std::max(1,gap)):std::max(size,default_pitch);
    return {size,pitch,5*pitch+2*std::max(0,padding)};
}
struct star_geometry {
    int left=0, pitch=1, count=0;
    int hit(int x) const { return x>=left && x<left+pitch*count ? (x-left)/pitch+1 : 0; }
};
inline star_geometry stars(int left,int width,int pitch,int alignment) {
    star_geometry g; g.pitch=std::max(1,pitch); g.count=std::clamp(width/g.pitch,0,5);
    const int spare=std::max(0,width-g.count*g.pitch);
    g.left=left+(alignment==1?spare:alignment==2?spare/2:0); return g;
}
}
