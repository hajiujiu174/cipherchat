#include "windows_process.hpp"
#include <shellapi.h>
#include <fstream>
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    try{
        namespace fs=std::filesystem;const auto root=ccfw::updates::executableDirectory();std::string pointer=".";
        if(fs::exists(root/"current.txt")){if(fs::file_size(root/"current.txt")>100)throw std::runtime_error("pointer");std::ifstream f(root/"current.txt",std::ios::binary);pointer.assign(std::istreambuf_iterator<char>(f),{});}
        const auto version=pointer.size()>18?pointer.substr(18):std::string{};
        if(pointer!="."&&(!pointer.starts_with(".updates/versions/")||version.empty()||version.size()>48||version.front()=='.'||version.back()=='.'||version.find("..")!=std::string::npos||version.find_first_not_of("0123456789.-")!=std::string::npos))throw std::runtime_error("pointer");
        const auto dir=root/pointer;for(auto p=dir;p!=root&&!p.empty();p=p.parent_path()){const auto a=GetFileAttributesW(p.c_str());if(a==INVALID_FILE_ATTRIBUTES||(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("directory");}
        int count=0;auto raw=CommandLineToArgvW(GetCommandLineW(),&count);if(!raw)throw std::runtime_error("arguments");std::vector<std::wstring> args;for(int n=1;n<count;++n)args.emplace_back(raw[n]);LocalFree(raw);
        const auto client=(pointer!="."&&fs::is_regular_file(dir/L"NEXTALK.exe"))?dir/L"NEXTALK.exe":dir/L"Messager.exe";
        ccfw::updates::spawn(client,args);return 0;
    }catch(...){MessageBoxW(nullptr,L"无法启动 NEXTALK。请检查安装目录，或使用更新窗口回退版本。",L"NEXTALK",MB_OK|MB_ICONERROR);return 1;}
}
