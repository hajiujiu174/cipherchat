#include "nextalkbrand.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#endif
namespace Messager {
void applyNextalkIdentity(){
#ifdef Q_OS_WIN
    SetCurrentProcessExplicitAppUserModelID(L"NEXTALK.Messenger");
#endif
}
void migrateNextalkShell(){
#ifdef Q_OS_WIN
    QDir dir(QCoreApplication::applicationDirPath());
    if(!QFileInfo::exists(dir.filePath("release.json"))||!QFileInfo::exists(dir.filePath("NEXTALK-branding.exe")))return;
    QDir root=dir;if(root.dirName().contains('.')){root.cdUp();if(root.dirName()=="versions"){root.cdUp();if(root.dirName()==".updates")root.cdUp();else return;}else return;}
    if(!QFileInfo::exists(root.filePath("current.txt")))return;
    const auto exe=dir.filePath("NEXTALK-branding.exe").toStdWString();
    std::wstring command=L"\""+exe+L"\" --root \""+root.absolutePath().toStdWString()+L"\"";
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    if(CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&startup,&process)){CloseHandle(process.hThread);CloseHandle(process.hProcess);}
#endif
}
}
