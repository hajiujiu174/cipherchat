"""Build a per-user x64 MSI with the existing Windows Installer SDK API and makecab."""
import argparse
import ctypes as c
from ctypes import wintypes as w
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import uuid

api = c.WinDLL("msi")
def function(name, result, *args):
    method = getattr(api, name); method.restype = result; method.argtypes = args; return method
open_database = function("MsiOpenDatabaseW", w.UINT, w.LPCWSTR, c.c_void_p, c.POINTER(w.UINT))
open_view = function("MsiDatabaseOpenViewW", w.UINT, w.UINT, w.LPCWSTR, c.POINTER(w.UINT))
execute = function("MsiViewExecute", w.UINT, w.UINT, w.UINT)
close = function("MsiCloseHandle", w.UINT, w.UINT)
record = function("MsiCreateRecord", w.UINT, w.UINT)
set_text = function("MsiRecordSetStringW", w.UINT, w.UINT, w.UINT, w.LPCWSTR)
set_integer = function("MsiRecordSetInteger", w.UINT, w.UINT, w.UINT, c.c_int)
set_stream = function("MsiRecordSetStreamW", w.UINT, w.UINT, w.UINT, w.LPCWSTR)
commit = function("MsiDatabaseCommit", w.UINT, w.UINT)
summary_info = function("MsiGetSummaryInformationW", w.UINT, w.UINT, w.LPCWSTR, w.UINT, c.POINTER(w.UINT))
summary_set = function("MsiSummaryInfoSetPropertyW", w.UINT, w.UINT, w.UINT, w.UINT, c.c_int, c.c_void_p, w.LPCWSTR)
summary_persist = function("MsiSummaryInfoPersist", w.UINT, w.UINT)
import_table = function("MsiDatabaseImportW", w.UINT, w.UINT, w.LPCWSTR, w.LPCWSTR)
last_error = function("MsiGetLastErrorRecord", w.UINT)
format_record = function("MsiFormatRecordW", w.UINT, w.UINT, w.UINT, w.LPWSTR, c.POINTER(w.DWORD))

def check(code):
    if code:
        rec = last_error(); size = w.DWORD(8192); text = c.create_unicode_buffer(size.value)
        if rec:
            format_record(0, rec, text, c.byref(size)); close(rec)
        raise RuntimeError(f"MSI error {code}: {text.value}")

class Database:
    def __init__(self, path):
        self.handle = w.UINT(); check(open_database(str(path), c.c_void_p(3), c.byref(self.handle)))
    def sql(self, query, values=()):
        view = w.UINT(); check(open_view(self.handle, query, c.byref(view))); rec = record(len(values))
        try:
            for index, value in enumerate(values, 1):
                if value is None: continue
                if isinstance(value, Path): check(set_stream(rec, index, str(value)))
                elif isinstance(value, int): check(set_integer(rec, index, value))
                else: check(set_text(rec, index, value))
            check(execute(view, rec if values else 0))
        finally:
            close(rec); close(view)
    def table(self, name, columns, keys):
        self.sql(f"CREATE TABLE `{name}` ({columns} PRIMARY KEY {keys})")
    def insert(self, name, columns, values):
        self.sql(f"INSERT INTO `{name}` ({','.join('`'+v+'`' for v in columns.split(','))}) VALUES ({','.join('?' for _ in values)})", values)

def identifier(path): return "F" + hashlib.sha256(path.encode()).hexdigest()[:24]
def guid(name): return "{" + str(uuid.uuid5(uuid.UUID("91f73744-5e16-4784-a941-ae0ed794187c"), name)).upper() + "}"

def build(package, output, work):
    if output.exists() or work.exists(): raise ValueError("Choose new output/work paths; existing installers are preserved")
    manifest = json.loads((package / "manifest.json").read_text(encoding="utf-8-sig"))
    release = json.loads((package / "release.json").read_bytes()); version = release["version"]
    for entry in manifest["files"]:
        path = package / entry["path"]
        if path.is_symlink() or not path.resolve().is_relative_to(package): raise ValueError("Unsafe package path")
        data = path.read_bytes()
        if len(data) != entry["bytes"] or hashlib.sha256(data).hexdigest() != entry["sha256"]: raise ValueError("Package hash mismatch: " + str(path))
    expected = {entry["path"] for entry in manifest["files"]} | {"manifest.json"}
    if {p.relative_to(package).as_posix() for p in package.rglob("*") if p.is_file()} != expected: raise ValueError("Unexpected package files")
    year, month, day, revision = map(int, version.split("."))
    msi_version = f"{year-2000}.{month}.{day*100+revision}"
    product = guid("product:" + version); upgrade = guid("upgrade:windows-x64")
    work.mkdir(parents=True); output.parent.mkdir(parents=True, exist_ok=True)
    compiler = Path("D:/msys64/mingw64/bin/g++.exe")
    env = dict(os.environ, PATH=str(compiler.parent) + os.pathsep + os.environ["PATH"])
    action = work / "installer-actions.dll"
    subprocess.run([str(compiler), "-std=c++20", "-O2", "-s", "-shared", "-static", "-static-libgcc", "-static-libstdc++",
                    str(Path(__file__).with_name("actions.cpp")), "-o", str(action), "-lmsi", "-lshell32", "-lole32", "-luuid"], check=True, env=env)
    icon = work / "CipherChat.ico"
    subprocess.run(["python", str(Path(__file__).with_name("make-icon.py")), str(icon)], check=True)
    owner = work / ".cipherchat-msi-owner"; owner.write_bytes(b"CipherChat-MSI-91f73744-5e16-4784-a941-ae0ed794187c\n")
    files = [(p, package / p) for p in sorted(expected)] + [("CipherChat.ico", icon), (owner.name, owner)]
    cab_source = work / "cab-input"; cab_source.mkdir()
    ddf = ['.OPTION EXPLICIT', '.Set Cabinet=ON', '.Set Compress=ON', '.Set CompressionType=LZX', '.Set CompressionMemory=21',
           '.Set CabinetNameTemplate=payload.cab', '.Set DiskDirectoryTemplate=.', '.Set MaxDiskSize=0', '.Set MaxCabinetSize=0']
    for relative, source in files:
        fid = identifier(relative); shutil.copyfile(source, cab_source / fid); ddf.append(f'"cab-input\\{fid}" "{fid}"')
    (work / "payload.ddf").write_text("\r\n".join(ddf) + "\r\n", encoding="ascii")
    subprocess.run(["makecab.exe", "/F", "payload.ddf"], cwd=work, check=True, stdout=(work / "makecab.log").open("w"))
    db = Database(output)
    try:
        (work / "codepage.idt").write_bytes(b"\r\n\r\n936\t_ForceCodepage\r\n")
        check(import_table(db.handle, str(work), "codepage.idt"))
        schemas = {
            "Property": ("`Property` CHAR(72) NOT NULL, `Value` CHAR(0) NOT NULL", "`Property`"),
            "Directory": ("`Directory` CHAR(72) NOT NULL, `Directory_Parent` CHAR(72), `DefaultDir` CHAR(255) NOT NULL", "`Directory`"),
            "Component": ("`Component` CHAR(72) NOT NULL, `ComponentId` CHAR(38), `Directory_` CHAR(72) NOT NULL, `Attributes` SHORT NOT NULL, `Condition` CHAR(255), `KeyPath` CHAR(72)", "`Component`"),
            "Feature": ("`Feature` CHAR(38) NOT NULL, `Feature_Parent` CHAR(38), `Title` CHAR(64), `Description` CHAR(255), `Display` SHORT, `Level` SHORT NOT NULL, `Directory_` CHAR(72), `Attributes` SHORT NOT NULL", "`Feature`"),
            "FeatureComponents": ("`Feature_` CHAR(38) NOT NULL, `Component_` CHAR(72) NOT NULL", "`Feature_`,`Component_`"),
            "File": ("`File` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `FileSize` LONG NOT NULL, `Version` CHAR(72), `Language` CHAR(20), `Attributes` SHORT, `Sequence` SHORT NOT NULL", "`File`"),
            "MsiFileHash": ("`File_` CHAR(72) NOT NULL, `Options` SHORT NOT NULL, `HashPart1` LONG NOT NULL, `HashPart2` LONG NOT NULL, `HashPart3` LONG NOT NULL, `HashPart4` LONG NOT NULL", "`File_`"),
            "Media": ("`DiskId` SHORT NOT NULL, `LastSequence` SHORT NOT NULL, `DiskPrompt` CHAR(64), `Cabinet` CHAR(255), `VolumeLabel` CHAR(32), `Source` CHAR(72)", "`DiskId`"),
            "Registry": ("`Registry` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Value` CHAR(0), `Component_` CHAR(72) NOT NULL", "`Registry`"),
            "Shortcut": ("`Shortcut` CHAR(72) NOT NULL, `Directory_` CHAR(72) NOT NULL, `Name` CHAR(128) NOT NULL, `Component_` CHAR(72) NOT NULL, `Target` CHAR(255) NOT NULL, `Arguments` CHAR(255), `Description` CHAR(255), `Hotkey` SHORT, `Icon_` CHAR(72), `IconIndex` SHORT, `ShowCmd` SHORT, `WkDir` CHAR(72)", "`Shortcut`"),
            "Icon": ("`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL", "`Name`"),
            "Binary": ("`Name` CHAR(72) NOT NULL, `Data` OBJECT NOT NULL", "`Name`"),
            "CustomAction": ("`Action` CHAR(72) NOT NULL, `Type` SHORT NOT NULL, `Source` CHAR(72), `Target` CHAR(255)", "`Action`"),
            "LaunchCondition": ("`Condition` CHAR(255) NOT NULL, `Description` CHAR(255) NOT NULL", "`Condition`"),
            "RemoveFile": ("`FileKey` CHAR(72) NOT NULL, `Component_` CHAR(72) NOT NULL, `FileName` CHAR(255), `DirProperty` CHAR(72) NOT NULL, `InstallMode` SHORT NOT NULL", "`FileKey`"),
            "Upgrade": ("`UpgradeCode` CHAR(38) NOT NULL, `VersionMin` CHAR(20), `VersionMax` CHAR(20), `Language` CHAR(255), `Attributes` LONG NOT NULL, `Remove` CHAR(255), `ActionProperty` CHAR(72) NOT NULL", "`UpgradeCode`,`VersionMin`,`VersionMax`,`Language`,`Attributes`"),
            "Dialog": ("`Dialog` CHAR(72) NOT NULL, `HCentering` SHORT NOT NULL, `VCentering` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG NOT NULL, `Title` CHAR(128), `Control_First` CHAR(50) NOT NULL, `Control_Default` CHAR(50), `Control_Cancel` CHAR(50)", "`Dialog`"),
            "Control": ("`Dialog_` CHAR(72) NOT NULL, `Control` CHAR(50) NOT NULL, `Type` CHAR(20) NOT NULL, `X` SHORT NOT NULL, `Y` SHORT NOT NULL, `Width` SHORT NOT NULL, `Height` SHORT NOT NULL, `Attributes` LONG, `Property` CHAR(72), `Text` CHAR(0), `Control_Next` CHAR(50), `Help` CHAR(50)", "`Dialog_`,`Control`"),
            "ControlEvent": ("`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, `Argument` CHAR(255) NOT NULL, `Condition` CHAR(255), `Ordering` SHORT", "`Dialog_`,`Control_`,`Event`,`Argument`,`Condition`"),
            "EventMapping": ("`Dialog_` CHAR(72) NOT NULL, `Control_` CHAR(50) NOT NULL, `Event` CHAR(50) NOT NULL, `Attribute` CHAR(50) NOT NULL", "`Dialog_`,`Control_`,`Event`,`Attribute`"),
            "TextStyle": ("`TextStyle` CHAR(72) NOT NULL, `FaceName` CHAR(32) NOT NULL, `Size` SHORT NOT NULL, `Color` LONG, `StyleBits` SHORT", "`TextStyle`"),
            "CheckBox": ("`Property` CHAR(72) NOT NULL, `Value` CHAR(64)", "`Property`"),
            "AppSearch": ("`Property` CHAR(72) NOT NULL, `Signature_` CHAR(72) NOT NULL", "`Property`,`Signature_`"),
            "RegLocator": ("`Signature_` CHAR(72) NOT NULL, `Root` SHORT NOT NULL, `Key` CHAR(255) NOT NULL, `Name` CHAR(255), `Type` SHORT", "`Signature_`"),
            "Signature": ("`Signature` CHAR(72) NOT NULL, `FileName` CHAR(255) NOT NULL, `MinVersion` CHAR(20), `MaxVersion` CHAR(20), `MinSize` LONG, `MaxSize` LONG, `MinDate` LONG, `MaxDate` LONG, `Languages` CHAR(255)", "`Signature`"),
        }
        for table in ("InstallExecuteSequence", "InstallUISequence", "AdminExecuteSequence", "AdminUISequence"):
            schemas[table] = ("`Action` CHAR(72) NOT NULL, `Condition` CHAR(255), `Sequence` SHORT", "`Action`")
        for table, (columns, keys) in schemas.items(): db.table(table, columns, keys)
        props = dict(ProductCode=product, UpgradeCode=upgrade, ProductName="CipherChat " + version, ProductVersion=msi_version,
                     ProductLanguage="2052", Manufacturer="CipherChat", INSTALLLEVEL="1", DefaultUIFont="Normal",
                     ARPPRODUCTICON="CipherChat.ico", ARPNOMODIFY="1", ARPCOMMENTS="端到端加密聊天；安装包版本 " + version,
                     ARPINSTALLLOCATION="[INSTALLDIR]", DESKTOPSHORTCUT="1", MSIRESTARTMANAGERCONTROL="DisableShutdown",
                     SecureCustomProperties="OLDERPRODUCTS;NEWERPRODUCTS;INSTALLDIR;DESKTOPSHORTCUT;MENUDIR;DESKTOPDIR", DiskPrompt="CipherChat 安装文件")
        for key, value in props.items(): db.insert("Property", "Property,Value", (key, value))
        db.insert("AppSearch", "Property,Signature_", ("WINDOWSBUILD", "WindowsBuild"))
        db.insert("RegLocator", "Signature_,Root,Key,Name,Type", ("WindowsBuild", 2, r"SOFTWARE\Microsoft\Windows NT\CurrentVersion", "CurrentBuildNumber", 18))
        # MSI does not remember command-line directory properties between sessions.
        for prop, name in [("SAVEDINSTALLDIR", "InstallDir"), ("SAVEDMENUDIR", "StartMenu"), ("SAVEDDESKTOPDIR", "Desktop"), ("SAVEDDESKTOPCHOICE", "DesktopChoice")]:
            db.insert("AppSearch", "Property,Signature_", (prop, prop))
            db.insert("RegLocator", "Signature_,Root,Key,Name,Type", (prop, 1, r"Software\CipherChat\Installer", name, 18))
        db.insert("LaunchCondition", "Condition,Description", ("VersionNT64 AND WINDOWSBUILD >= 10240", "需要 Windows 10 / 11 64 位系统。"))
        db.insert("LaunchCondition", "Condition,Description", ("NOT NEWERPRODUCTS", "已安装更新的 CipherChat 安装包。请使用现有程序或先卸载它。"))
        db.insert("LaunchCondition", "Condition,Description", ("NOT ALLUSERS", "此安装包仅支持当前用户安装，无需管理员权限。"))
        db.insert("Upgrade", "UpgradeCode,VersionMin,VersionMax,Language,Attributes,Remove,ActionProperty", (upgrade, None, msi_version, None, 1, "ALL", "OLDERPRODUCTS"))
        db.insert("Upgrade", "UpgradeCode,VersionMin,VersionMax,Language,Attributes,Remove,ActionProperty", (upgrade, msi_version, None, None, 2, None, "NEWERPRODUCTS"))
        directories = [("TARGETDIR", None, "SourceDir"), ("LocalAppDataFolder", "TARGETDIR", "."), ("ProgramsDir", "LocalAppDataFolder", "Programs"),
                       ("INSTALLDIR", "ProgramsDir", "CipherChat"), ("ProgramMenuFolder", "TARGETDIR", "."), ("MENUDIR", "ProgramMenuFolder", "CipherChat"), ("DesktopFolder", "TARGETDIR", "."), ("DESKTOPDIR", "DesktopFolder", ".")]
        dir_ids = {".": "INSTALLDIR"}
        for relative, _ in files:
            parent = Path(relative).parent
            for path in reversed([parent, *parent.parents]):
                name = path.as_posix()
                if name == "." or name in dir_ids: continue
                did = "D" + identifier(name)[1:]; dir_ids[name] = did
                directories.append((did, dir_ids[path.parent.as_posix()], path.name))
        for row in directories: db.insert("Directory", "Directory,Directory_Parent,DefaultDir", row)
        db.insert("Feature", "Feature,Feature_Parent,Title,Description,Display,Level,Directory_,Attributes", ("Main", None, "CipherChat", "程序、加密库与增量更新组件", 1, 1, "INSTALLDIR", 0))
        class Hash(c.Structure): _fields_ = [("size", w.DWORD), ("data", w.DWORD * 4)]
        file_hash = function("MsiGetFileHashW", w.UINT, w.LPCWSTR, w.DWORD, c.POINTER(Hash))
        for sequence, (relative, source) in enumerate(files, 1):
            fid = identifier(relative); component = "C" + fid[1:]
            db.insert("Component", "Component,ComponentId,Directory_,Attributes,Condition,KeyPath", (component, guid("file:" + relative), dir_ids[Path(relative).parent.as_posix()], 256, None, fid))
            db.insert("FeatureComponents", "Feature_,Component_", ("Main", component))
            db.insert("File", "File,Component_,FileName,FileSize,Version,Language,Attributes,Sequence", (fid, component, Path(relative).name, source.stat().st_size, None, None, 16384, sequence))
            info = Hash(); info.size = c.sizeof(Hash); check(file_hash(str(source), 0, c.byref(info)))
            db.insert("MsiFileHash", "File_,Options,HashPart1,HashPart2,HashPart3,HashPart4", (fid, 0, *(c.c_int(v).value for v in info.data)))
        db.insert("Media", "DiskId,LastSequence,DiskPrompt,Cabinet,VolumeLabel,Source", (1, len(files), None, "#payload.cab", None, None))
        db.insert("_Streams", "Name,Data", ("payload.cab", work / "payload.cab"))
        db.insert("Icon", "Name,Data", ("CipherChat.ico", icon))
        db.insert("Binary", "Name,Data", ("InstallerActions", action))
        for component, directory, condition, key in [("MenuShortcut", "MENUDIR", None, "StartMenu"), ("DesktopShortcut", "DESKTOPDIR", 'DESKTOPSHORTCUT="1"', "Desktop")]:
            db.insert("Component", "Component,ComponentId,Directory_,Attributes,Condition,KeyPath", (component, guid(component), directory, 260, condition, component + "Reg"))
            db.insert("FeatureComponents", "Feature_,Component_", ("Main", component))
            db.insert("Registry", "Registry,Root,Key,Name,Value,Component_", (component + "Reg", 1, r"Software\CipherChat\Installer", key, "[" + directory + "]", component))
            db.insert("Shortcut", "Shortcut,Directory_,Name,Component_,Target,Arguments,Description,Hotkey,Icon_,IconIndex,ShowCmd,WkDir",
                      (component, directory, "CipherChat", component, "[INSTALLDIR]cipherchat-launcher.exe", None, "CipherChat 加密聊天", None, "CipherChat.ico", 0, 1, "INSTALLDIR"))
        db.insert("RemoveFile", "FileKey,Component_,FileName,DirProperty,InstallMode", ("RemoveMenuDir", "MenuShortcut", None, "MENUDIR", 2))
        launcher_component = "C" + identifier("cipherchat-launcher.exe")[1:]
        for key, value in [("InstallDir", "[INSTALLDIR]"), ("DesktopChoice", "enabled=[DESKTOPSHORTCUT]")]:
            db.insert("Registry", "Registry,Root,Key,Name,Value,Component_", (key + "Reg", 1, r"Software\CipherChat\Installer", key, value, launcher_component))
        for name in ("current.txt", "previous.txt"):
            db.insert("RemoveFile", "FileKey,Component_,FileName,DirProperty,InstallMode", (identifier(name), "C" + identifier("cipherchat-launcher.exe")[1:], name, "INSTALLDIR", 2))
        for action_row in [("ValidateLocation", 1, "InstallerActions", "ValidateLocation"), ("SetARPInstallLocation", 51, "ARPINSTALLLOCATION", "[INSTALLDIR]"), ("SetCleanupPath", 51, "CleanUpdates", "[INSTALLDIR]"),
                           ("CleanUpdates", 1537, "InstallerActions", "CleanUpdates")]: db.insert("CustomAction", "Action,Type,Source,Target", action_row)
        restore_actions = []
        for index, prop in enumerate(("INSTALLDIR", "MENUDIR", "DESKTOPDIR")):
            action_name = "Restore" + prop
            db.insert("CustomAction", "Action,Type,Source,Target", (action_name, 51, prop, "[SAVED" + prop + "]"))
            restore_actions.append((action_name, "(Installed OR OLDERPRODUCTS) AND SAVED" + prop, 60 + index))
        db.insert("CustomAction", "Action,Type,Source,Target", ("RestoreDesktopChoice", 51, "DESKTOPSHORTCUT", None))
        restore_actions.append(("RestoreDesktopChoice", '(Installed OR OLDERPRODUCTS) AND SAVEDDESKTOPCHOICE="enabled="', 63))
        execute_actions = [("FindRelatedProducts", None, 25), ("AppSearch", None, 50), ("LaunchConditions", None, 100), ("ValidateProductID", None, 700), ("CostInitialize", None, 800),
                           ("FileCost", None, 900), ("CostFinalize", None, 1000), ("MigrateFeatureStates", None, 1200), ("ValidateLocation", None, 1300),
                           ("SetARPInstallLocation", None, 1310), ("InstallValidate", None, 1400), ("RemoveExistingProducts", "OLDERPRODUCTS", 1450), ("InstallInitialize", None, 1500),
                           ("ProcessComponents", None, 1600), ("UnpublishFeatures", None, 1800), ("RemoveShortcuts", None, 3200), ("RemoveRegistryValues", None, 2600),
                           ("RemoveFiles", None, 3500), ("InstallFiles", None, 4000), ("CreateShortcuts", None, 4500), ("WriteRegistryValues", None, 5000),
                           ("RegisterUser", None, 6000), ("RegisterProduct", None, 6100), ("PublishFeatures", None, 6300), ("PublishProduct", None, 6400),
                           ("SetCleanupPath", 'REMOVE~="ALL" AND NOT UPGRADINGPRODUCTCODE AND CAN_CLEAN_UPDATES="1"', 6500),
                           ("CleanUpdates", 'REMOVE~="ALL" AND NOT UPGRADINGPRODUCTCODE AND CAN_CLEAN_UPDATES="1"', 6510), ("InstallFinalize", None, 6600)]
        for row in execute_actions: db.insert("InstallExecuteSequence", "Action,Condition,Sequence", row)
        for table in ("InstallExecuteSequence", "InstallUISequence"):
            for row in restore_actions: db.insert(table, "Action,Condition,Sequence", row)
        for row in [("FindRelatedProducts", None, 25), ("AppSearch", None, 50), ("LaunchConditions", None, 100), ("CostInitialize", None, 800), ("FileCost", None, 900),
                    ("CostFinalize", None, 1000), ("MigrateFeatureStates", None, 1200), ("WelcomeDlg", "NOT Installed", 1230),
                    ("MaintenanceDlg", 'Installed AND NOT REMOVE', 1240), ("ProgressDlg", None, 1280), ("ExecuteAction", None, 1300),
                    ("ExitDlg", None, -1), ("CancelDlg", None, -2), ("FatalDlg", None, -3)]: db.insert("InstallUISequence", "Action,Condition,Sequence", row)
        for row in [("CostInitialize", None, 800), ("FileCost", None, 900), ("CostFinalize", None, 1000), ("InstallValidate", None, 1400),
                    ("InstallInitialize", None, 1500), ("InstallAdminPackage", None, 3900), ("InstallFiles", None, 4000), ("InstallFinalize", None, 6600)]: db.insert("AdminExecuteSequence", "Action,Condition,Sequence", row)
        for row in [("CostInitialize", None, 800), ("FileCost", None, 900), ("CostFinalize", None, 1000), ("ExecuteAction", None, 1300)]: db.insert("AdminUISequence", "Action,Condition,Sequence", row)
        for row in [("Normal", "Microsoft YaHei UI", 9, 0x303030, 0), ("Title", "Microsoft YaHei UI", 18, 0x6d7a1f, 1)]: db.insert("TextStyle", "TextStyle,FaceName,Size,Color,StyleBits", row)
        db.insert("CheckBox", "Property,Value", ("DESKTOPSHORTCUT", "1"))
        def dialog(name, first, title="CipherChat 安装", modal=True):
            db.insert("Dialog", "Dialog,HCentering,VCentering,Width,Height,Attributes,Title,Control_First,Control_Default,Control_Cancel", (name, 50, 50, 390, 270, 3 if modal else 1, title, first, first, "Cancel" if name in ("WelcomeDlg", "MaintenanceDlg") else first))
        def control(dlg, name, kind, x, y, width, height, text=None, prop=None, next=None, attrs=3):
            db.insert("Control", "Dialog_,Control,Type,X,Y,Width,Height,Attributes,Property,Text,Control_Next,Help", (dlg, name, kind, x, y, width, height, attrs, prop, text, next, None))
        def event(dlg, name, evt, arg, order=1): db.insert("ControlEvent", "Dialog_,Control_,Event,Argument,Condition,Ordering", (dlg, name, evt, arg, "1", order))
        dialog("WelcomeDlg", "Install")
        control("WelcomeDlg", "Heading", "Text", 24, 22, 340, 35, r"{\Title}安装 CipherChat")
        control("WelcomeDlg", "Description", "Text", 24, 67, 342, 55, f"版本 {version} · Windows 64 位\n私密聊天、文件传输、好友申请及软件内增量更新。\n安装到当前用户，无需管理员权限。")
        control("WelcomeDlg", "Location", "Text", 24, 125, 342, 40, "程序位置：[INSTALLDIR]")
        control("WelcomeDlg", "DataNote", "Text", 24, 171, 342, 30, "沿用原有账号及聊天记录；卸载时保留用户数据。")
        control("WelcomeDlg", "Desktop", "CheckBox", 24, 207, 245, 18, "创建桌面快捷方式", "DESKTOPSHORTCUT", "Install")
        control("WelcomeDlg", "Install", "PushButton", 225, 237, 70, 22, "安装", next="Cancel")
        control("WelcomeDlg", "Cancel", "PushButton", 302, 237, 65, 22, "取消", next="Desktop")
        event("WelcomeDlg", "Install", "EndDialog", "Return"); event("WelcomeDlg", "Cancel", "EndDialog", "Exit")
        dialog("MaintenanceDlg", "Repair")
        control("MaintenanceDlg", "Heading", "Text", 24, 22, 342, 35, r"{\Title}维护 CipherChat")
        control("MaintenanceDlg", "Description", "Text", 24, 78, 342, 95, "修复：恢复此安装包的基础程序文件。\n\n卸载：移除程序与更新缓存，保留账号、聊天记录和下载文件。\n\n操作前请先退出 CipherChat。")
        for name, x, text, next in [("Repair", 145, "修复", "Remove"), ("Remove", 222, "卸载", "Cancel"), ("Cancel", 299, "取消", "Repair")]: control("MaintenanceDlg", name, "PushButton", x, 237, 70, 22, text, next=next)
        event("MaintenanceDlg", "Repair", "[REINSTALL]", "ALL"); event("MaintenanceDlg", "Repair", "[REINSTALLMODE]", "amus", 2); event("MaintenanceDlg", "Repair", "EndDialog", "Return", 3)
        event("MaintenanceDlg", "Remove", "[REMOVE]", "ALL"); event("MaintenanceDlg", "Remove", "EndDialog", "Return", 2); event("MaintenanceDlg", "Cancel", "EndDialog", "Exit")
        dialog("ProgressDlg", "Heading", modal=False)
        control("ProgressDlg", "Heading", "Text", 24, 30, 342, 35, r"{\Title}正在处理…")
        control("ProgressDlg", "Description", "Text", 24, 85, 342, 45, "Windows 正在安装、修复或卸载 CipherChat，请稍候。")
        control("ProgressDlg", "Progress", "ProgressBar", 24, 155, 342, 18, "", attrs=65537)
        control("ProgressDlg", "Action", "Text", 24, 184, 342, 40, "准备程序文件…")
        db.insert("EventMapping", "Dialog_,Control_,Event,Attribute", ("ProgressDlg", "Progress", "SetProgress", "Progress"))
        db.insert("EventMapping", "Dialog_,Control_,Event,Attribute", ("ProgressDlg", "Action", "ActionText", "Text"))
        for name, title, description in [("ExitDlg", "操作已完成", "安装后可通过桌面或开始菜单打开 CipherChat。\n\n如刚完成卸载，账号与聊天记录仍保留在原数据目录。"),
                                         ("CancelDlg", "已取消", "安装操作已取消。"), ("FatalDlg", "操作未完成", "请检查磁盘空间，并退出正在运行的 CipherChat 后重试。\n\n如问题持续，可保留安装日志用于排查。")]:
            dialog(name, "Close"); control(name, "Heading", "Text", 24, 30, 342, 35, "{\\Title}" + title)
            control(name, "Description", "Text", 24, 85, 342, 95, description)
            control(name, "Close", "PushButton", 297, 237, 70, 22, "完成", next="Close"); event(name, "Close", "EndDialog", "Return")
        # Use Windows Installer's built-in error dialog (all response-button combinations supported).
        check(commit(db.handle))
        summary = w.UINT(); check(summary_info(db.handle, None, 20, c.byref(summary)))
        try:
            for pid, val in [(1, 936), (14, 500), (15, 10), (19, 2)]: check(summary_set(summary, pid, 2 if pid == 1 else 3, val, None, None))
            for pid, val in [(2, "CipherChat 安装器"), (3, "CipherChat " + version), (4, "CipherChat"), (7, "x64;2052"), (9, "{" + str(uuid.uuid4()).upper() + "}"), (18, "CipherChat MSI Builder")]: check(summary_set(summary, pid, 30, 0, None, val))
            check(summary_persist(summary))
        finally: close(summary)
        check(commit(db.handle))
    finally: close(db.handle)
    report = dict(version=version, msi_version=msi_version, product_code=product, upgrade_code=upgrade, files=len(files),
                  per_user=True, administrator_required=False, bytes=output.stat().st_size, sha256=hashlib.sha256(output.read_bytes()).hexdigest(),
                  source_package=str(package), output=str(output))
    output.with_suffix(".json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report))

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    for flag in ("package", "output", "work"): parser.add_argument("--" + flag, type=Path, required=True)
    args = parser.parse_args(); build(args.package.resolve(), args.output.resolve(), args.work.resolve())
