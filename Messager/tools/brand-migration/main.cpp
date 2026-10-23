#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#include <shobjidl.h>
#include <propkey.h>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include "nlohmann/json.hpp"
namespace fs=std::filesystem;using Json=nlohmann::json;
namespace {
std::wstring lower(std::wstring v){for(auto& c:v)c=std::towlower(c);return v;}
std::wstring normal(const fs::path& p){return lower(fs::weakly_canonical(p).wstring());}
bool inside(const fs::path& p,const fs::path& root){const auto a=normal(p),b=normal(root);return a==b||(a.size()>b.size()&&a.compare(0,b.size(),b)==0&&(a[b.size()]=='\\'||a[b.size()]=='/'));}
std::string utf8(const fs::path& p){const auto v=p.u8string();return std::string(v.begin(),v.end());}
std::string contents(const fs::path& p){std::ifstream f(p,std::ios::binary);return {std::istreambuf_iterator<char>(f),{}};}
void jsonFile(const fs::path& p,const Json& j){fs::create_directories(p.parent_path());std::ofstream f(p,std::ios::binary);f<<j.dump(2);if(!f)throw std::runtime_error("cannot write migration report");}
fs::path known(REFKNOWNFOLDERID id){PWSTR p=nullptr;if(FAILED(SHGetKnownFolderPath(id,0,nullptr,&p)))return {};fs::path result=p;CoTaskMemFree(p);return result;}
void regularParents(const fs::path& p,const fs::path& root){
    if(!inside(p,root))throw std::runtime_error("outside installation");
    for(auto at=p;inside(at,root);at=at.parent_path()){
        const auto a=GetFileAttributesW(at.c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("linked installation directory");
        if(normal(at)==normal(root))break;
    }
}
void copyAtomic(const fs::path& from,const fs::path& to){
    if(fs::exists(to)&&contents(from)==contents(to))return;
    auto temp=to;temp+=L".nextalk-tmp";fs::copy_file(from,temp,fs::copy_options::overwrite_existing);
    if(!MoveFileExW(temp.c_str(),to.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("cannot refresh stable NEXTALK launcher/icon");
}
void backup(const fs::path& p,const fs::path& directory){
    if(!fs::exists(p))return;fs::create_directories(directory);
    const auto name=std::to_wstring(std::hash<std::wstring>{}(normal(p)))+L"-"+p.filename().wstring();
    const auto out=directory/name;if(!fs::exists(out))fs::copy_file(p,out);
}
bool migrateLink(const fs::path& link,const fs::path& scanRoot,bool menu,bool pinned,const fs::path& root,const fs::path& legacy,const fs::path& backupDir,Json& changed){
    const auto name=lower(link.stem().wstring());if(name.find(L"cipherchat")==std::wstring::npos&&name.find(L"messager")==std::wstring::npos&&name.find(L"nextalk")==std::wstring::npos)return false;
    IShellLinkW* shell=nullptr;IPersistFile* file=nullptr;
    if(FAILED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&shell))))return false;
    auto release=[&]{if(file)file->Release();shell->Release();};
    if(FAILED(shell->QueryInterface(IID_PPV_ARGS(&file)))||FAILED(file->Load(link.c_str(),STGM_READ))){release();return false;}
    wchar_t target[32768]{},arguments[32768]{},icon[32768]{};int iconIndex=0;
    shell->GetPath(target,32768,nullptr,SLGP_RAWPATH);shell->GetArguments(arguments,32768);shell->GetIconLocation(icon,32768,&iconIndex);
    if(!*target||(!inside(target,root)&&(legacy.empty()||normal(target)!=normal(legacy)))){release();return false;}
    std::wstring args=arguments;
    if(!legacy.empty()&&normal(target)==normal(legacy)){
        if(args==L"--ui messager"||args==L"--ui codex")args.clear();
        else if(!args.empty()){release();return false;}
    }
    const auto launcher=root/L"NEXTALK.exe",newIcon=root/L"NEXTALK.ico";
    fs::path out=link;if(!pinned)out=menu?scanRoot/L"NEXTALK"/L"NEXTALK.lnk":scanRoot/L"NEXTALK.lnk";
    IPropertyStore* props=nullptr;bool identity=false;
    if(SUCCEEDED(shell->QueryInterface(IID_PPV_ARGS(&props)))){PROPVARIANT old{};if(SUCCEEDED(props->GetValue(PKEY_AppUserModel_ID,&old)))identity=old.vt==VT_LPWSTR&&old.pwszVal&&std::wstring(old.pwszVal)==L"NEXTALK.Messenger";PropVariantClear(&old);}
    const bool done=normal(target)==normal(launcher)&&normal(link)==normal(out)&&*icon&&normal(icon)==normal(newIcon)&&identity;
    if(done){if(props)props->Release();release();return false;}
    if(fs::exists(out)&&normal(out)!=normal(link)){
        IShellLinkW* other=nullptr;IPersistFile* otherFile=nullptr;bool ours=false;
        if(SUCCEEDED(CoCreateInstance(CLSID_ShellLink,nullptr,CLSCTX_INPROC_SERVER,IID_PPV_ARGS(&other)))){
            if(SUCCEEDED(other->QueryInterface(IID_PPV_ARGS(&otherFile)))&&SUCCEEDED(otherFile->Load(out.c_str(),STGM_READ))){wchar_t path[32768]{};other->GetPath(path,32768,nullptr,SLGP_RAWPATH);ours=*path&&inside(path,root);}if(otherFile)otherFile->Release();other->Release();}
        if(!ours){if(props)props->Release();release();throw std::runtime_error("NEXTALK shortcut name is occupied by another application");}
    }
    backup(link,backupDir);if(fs::exists(out))backup(out,backupDir);
    fs::create_directories(out.parent_path());shell->SetPath(launcher.c_str());shell->SetArguments(args.c_str());shell->SetWorkingDirectory(root.c_str());shell->SetDescription(L"NEXTALK");shell->SetIconLocation(newIcon.c_str(),0);
    if(props){PROPVARIANT id{};id.vt=VT_LPWSTR;id.pwszVal=const_cast<wchar_t*>(L"NEXTALK.Messenger");props->SetValue(PKEY_AppUserModel_ID,id);props->Commit();props->Release();}
    const HRESULT result=file->Save(out.c_str(),TRUE);release();if(FAILED(result))throw std::runtime_error("cannot save NEXTALK shortcut");
    IPropertyStore* persisted=nullptr;
    if(FAILED(SHGetPropertyStoreFromParsingName(out.c_str(),nullptr,GPS_READWRITE,IID_PPV_ARGS(&persisted))))throw std::runtime_error("cannot open shortcut identity");
    PROPVARIANT identityValue{};identityValue.vt=VT_LPWSTR;identityValue.pwszVal=const_cast<wchar_t*>(L"NEXTALK.Messenger");
    const auto identitySet=persisted->SetValue(PKEY_AppUserModel_ID,identityValue);const auto identityCommit=persisted->Commit();persisted->Release();
    if(FAILED(identitySet)||FAILED(identityCommit))throw std::runtime_error("cannot persist shortcut identity");
    if(normal(out)!=normal(link)){fs::remove(link);if(menu&&link.parent_path()!=scanRoot&&fs::is_empty(link.parent_path()))fs::remove(link.parent_path());}
    changed.push_back({{"old",utf8(link)},{"new",utf8(out)},{"target",utf8(launcher)},{"icon",utf8(newIcon)},{"pinned",pinned}});return true;
}
std::wstring registryText(HKEY key,const wchar_t* name){DWORD type=0,size=0;if(RegQueryValueExW(key,name,nullptr,&type,nullptr,&size)!=ERROR_SUCCESS||(type!=REG_SZ&&type!=REG_EXPAND_SZ)||size>65536)return {};std::wstring v(size/2+1,L'\0');if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(v.data()),&size)!=ERROR_SUCCESS)return {};v.resize(wcslen(v.c_str()));return v;}
void updateRegistry(const fs::path& root,const fs::path& backupDir,Json& changes){
    HKEY parent=nullptr;if(RegOpenKeyExW(HKEY_CURRENT_USER,L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",0,KEY_READ,&parent)!=ERROR_SUCCESS)return;
    std::vector<std::wstring> keys;for(DWORD n=0;;++n){wchar_t name[256];DWORD len=256;if(RegEnumKeyExW(parent,n,name,&len,nullptr,nullptr,nullptr,nullptr)!=ERROR_SUCCESS)break;keys.emplace_back(name,len);}
    for(const auto& name:keys){HKEY key=nullptr;if(RegOpenKeyExW(parent,name.c_str(),0,KEY_QUERY_VALUE|KEY_SET_VALUE,&key)!=ERROR_SUCCESS)continue;
        const auto location=registryText(key,L"InstallLocation"),display=registryText(key,L"DisplayName");
        if(!location.empty()&&normal(location)==normal(root)&&(display.find(L"CipherChat")!=std::wstring::npos||display.find(L"NEXTALK")!=std::wstring::npos)){
            const auto icon=(root/L"NEXTALK.ico").wstring();const auto oldIcon=registryText(key,L"DisplayIcon");
            if(display!=L"NEXTALK"||oldIcon!=icon){const auto store=backupDir/(name+L"-registry.json");if(!fs::exists(store))jsonFile(store,{{"key",utf8(name)},{"DisplayName",utf8(display)},{"DisplayIcon",utf8(oldIcon)}});
                RegSetValueExW(key,L"DisplayName",0,REG_SZ,reinterpret_cast<const BYTE*>(L"NEXTALK"),16);RegSetValueExW(key,L"DisplayIcon",0,REG_SZ,reinterpret_cast<const BYTE*>(icon.c_str()),DWORD((icon.size()+1)*2));changes.push_back(utf8(name));}
        }RegCloseKey(key);
    }RegCloseKey(parent);
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
    fs::path report;Json result={{"brand","NEXTALK"},{"links",Json::array()},{"registry",Json::array()},{"errors",Json::array()}};
    try{
        int count=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&count);fs::path root,desktop,menu,legacy;bool fixture=false;
        for(int i=1;i<count;++i){const std::wstring option=argv[i];if(i+1>=count)throw std::runtime_error("missing option value");const fs::path value=argv[++i];
            if(option==L"--root")root=value;else if(option==L"--report")report=value;else if(option==L"--desktop"){desktop=value;fixture=true;}else if(option==L"--menu"){menu=value;fixture=true;}else if(option==L"--legacy-launcher")legacy=value;else throw std::runtime_error("unknown migration option");}
        LocalFree(argv);if(root.empty()||!fs::is_regular_file(root/L"current.txt"))throw std::runtime_error("not an installed NEXTALK root");root=fs::weakly_canonical(root);
        wchar_t module[32768]{};GetModuleFileNameW(nullptr,module,32768);const auto payload=fs::path(module).parent_path();
        if(!fs::is_regular_file(payload/L"release.json")||!fs::is_regular_file(payload/L"NEXTALK-launcher.exe")||!fs::is_regular_file(payload/L"NEXTALK.ico"))throw std::runtime_error("incomplete NEXTALK payload");
        regularParents(root/L".updates",root);const auto backupDir=root/L".updates"/L"branding-backup";fs::create_directories(backupDir);regularParents(backupDir,root);
        HANDLE lock=CreateMutexW(nullptr,TRUE,(L"Local\\NEXTALKBrand-"+std::to_wstring(std::hash<std::wstring>{}(normal(root)))).c_str());
        if(!lock)throw std::runtime_error("cannot lock migration");if(GetLastError()==ERROR_ALREADY_EXISTS){CloseHandle(lock);return 0;}
        CoInitializeEx(nullptr,COINIT_APARTMENTTHREADED);
        backup(root/L"NEXTALK.exe",backupDir);copyAtomic(payload/L"NEXTALK-launcher.exe",root/L"NEXTALK.exe");copyAtomic(payload/L"NEXTALK.ico",root/L"NEXTALK.ico");
        if(!fixture){desktop=known(FOLDERID_Desktop);menu=known(FOLDERID_Programs);}
        struct Scan {fs::path path;bool menu,pinned;};std::vector<Scan> scans{{desktop,false,false},{menu,true,false}};
        if(!fixture){const auto appData=known(FOLDERID_RoamingAppData);scans.push_back({appData/L"Microsoft/Internet Explorer/Quick Launch/User Pinned/TaskBar",false,true});}
        for(const auto& scan:scans){if(scan.path.empty()||!fs::is_directory(scan.path))continue;std::vector<fs::path> links;
            for(auto it=fs::recursive_directory_iterator(scan.path,fs::directory_options::skip_permission_denied);it!=fs::recursive_directory_iterator();++it){if(it.depth()>6){it.disable_recursion_pending();continue;}const auto a=GetFileAttributesW(it->path().c_str());if(a!=INVALID_FILE_ATTRIBUTES&&(a&FILE_ATTRIBUTE_REPARSE_POINT)){it.disable_recursion_pending();continue;}if(it->is_regular_file()&&lower(it->path().extension().wstring())==L".lnk")links.push_back(it->path());}
            for(const auto& link:links)try{migrateLink(link,scan.path,scan.menu,scan.pinned,root,legacy,backupDir,result["links"]);}catch(const std::exception& e){result["errors"].push_back({{"file",utf8(link)},{"reason",e.what()}});}
        }
        if(!fixture)updateRegistry(root,backupDir,result["registry"]);
        SHChangeNotify(SHCNE_ASSOCCHANGED,SHCNF_IDLIST,nullptr,nullptr);result["ok"]=result["errors"].empty();result["root"]=utf8(root);
        jsonFile(root/L".updates"/L"nextalk-branding.json",result);if(!report.empty())jsonFile(report,result);
        CoUninitialize();ReleaseMutex(lock);CloseHandle(lock);return result["ok"].get<bool>()?0:2;
    }catch(const std::exception& e){result["ok"]=false;result["errors"].push_back(e.what());if(!report.empty())try{jsonFile(report,result);}catch(...){}return 1;}
}
