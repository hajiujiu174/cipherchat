#pragma once
#include <cstdint>
#include <stdexcept>
#include <string_view>
namespace cc::server {
inline void validateSocialText(std::string_view text, std::size_t limit, bool multiline) {
    std::size_t count=0;bool visible=false;
    for(std::size_t i=0;i<text.size();) {
        const auto lead=static_cast<unsigned char>(text[i++]);std::uint32_t code=lead;unsigned extra=0;
        if(lead>=0xc2 && lead<=0xdf){code=lead&31;extra=1;}
        else if(lead>=0xe0 && lead<=0xef){code=lead&15;extra=2;}
        else if(lead>=0xf0 && lead<=0xf4){code=lead&7;extra=3;}
        else if(lead>=0x80)throw std::runtime_error("invalid_text");
        if(i+extra>text.size())throw std::runtime_error("invalid_text");
        for(unsigned n=0;n<extra;++n){auto c=static_cast<unsigned char>(text[i++]);if((c&0xc0)!=0x80)throw std::runtime_error("invalid_text");code=(code<<6)|(c&63);}
        if((extra==1&&code<0x80)||(extra==2&&code<0x800)||(extra==3&&code<0x10000)||code>0x10ffff||(code>=0xd800&&code<=0xdfff))throw std::runtime_error("invalid_text");
        if((code<32 && !(multiline&&(code==10||code==9)))||code==127)throw std::runtime_error("invalid_text");
        if(code>32 && code!=0x3000 && code!=0xa0)visible=true;
        if(++count>limit)throw std::runtime_error("text_too_long");
    }
    if(!visible)throw std::runtime_error("empty_text");
}
inline void validateProfilePng(std::string_view png) {
    if(png.empty())return;
    if(png.size()>288*1024 || png.size()<33 || png.substr(0,8)!=std::string_view("\x89PNG\r\n\x1a\n",8)||png.substr(12,4)!="IHDR")throw std::runtime_error("invalid_avatar");
    auto integer=[&](std::size_t at){std::uint32_t n=0;for(unsigned i=0;i<4;++i)n=(n<<8)|static_cast<unsigned char>(png[at+i]);return n;};
    if(integer(8)!=13 || !integer(16)||!integer(20)||integer(16)>256||integer(20)>256)throw std::runtime_error("invalid_avatar");
    std::size_t at=8;bool image=false;
    while(at+12<=png.size()) {
        const auto size=integer(at);if(size>png.size()-at-12)throw std::runtime_error("invalid_avatar");
        const auto kind=png.substr(at+4,4);if(kind=="IDAT")image=true;
        at+=size+12;
        if(kind=="IEND"){if(size||at!=png.size()||!image)throw std::runtime_error("invalid_avatar");return;}
    }
    throw std::runtime_error("invalid_avatar");
}
}
