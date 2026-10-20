using System;
using System.Collections;
using System.Collections.Generic;
using System.Diagnostics;
using System.Drawing;
using System.IO;
using System.Text;
using System.Text.RegularExpressions;
using System.Web.Script.Serialization;
using System.Windows.Forms;

// Configuration-only, per-user migration. Never opens an account database or registry key.
static class Migration {
    public const string Host = "8.160.172.25", Name = "2603:1040:f02:d::21d";
    static readonly JavaScriptSerializer Json = new JavaScriptSerializer();
    public static Dictionary<string, object> Read(string path) {
        if (new FileInfo(path).Length > 1048576) throw new Exception("配置文件过大：" + path);
        return Json.Deserialize<Dictionary<string, object>>(File.ReadAllText(path));
    }
    static string Value(Dictionary<string, object> o, string k) {
        return o.ContainsKey(k) ? Convert.ToString(o[k]) : "";
    }
    static void Safe(string path) {
        for (string p = Path.GetFullPath(path); !String.IsNullOrEmpty(p); p = Path.GetDirectoryName(p))
            if ((File.Exists(p) || Directory.Exists(p)) && (File.GetAttributes(p) & FileAttributes.ReparsePoint) != 0)
                throw new Exception("请选择实际安装目录，不支持链接或重解析目录：" + p);
    }
    public static List<string> Resolve(string selected) {
        var result = new List<string>();
        ResolveOne(Path.GetFullPath(selected), result, new HashSet<string>(StringComparer.OrdinalIgnoreCase), 0);
        if (result.Count == 0) throw new Exception("没有找到 connection.json。请选择 CipherChat 的安装目录（含程序或启动器）。");
        return result;
    }
    static void ResolveOne(string dir, List<string> result, HashSet<string> seen, int depth) {
        if (depth > 6 || !seen.Add(dir)) throw new Exception("启动目录存在循环或层级过深。");
        Safe(dir);
        if (!Directory.Exists(dir)) throw new Exception("目录不存在：" + dir);
        bool program = File.Exists(Path.Combine(dir, "Messager.exe")) || File.Exists(Path.Combine(dir, "cipherchat-launcher.exe")) || File.Exists(Path.Combine(dir, "CipherChat.exe"));
        if (program && File.Exists(Path.Combine(dir, "connection.json"))) result.Add(dir);
        string selector = Path.Combine(dir, "desktop-frontends.json");
        if (File.Exists(selector)) {
            var o = Read(selector);
            if (o.ContainsKey("frontends")) foreach (var item in (IEnumerable)o["frontends"]) {
                var f = (Dictionary<string, object>)item;
                if (Value(f, "id") != "messager") continue;
                string target = Value(f, "program");
                if (String.IsNullOrWhiteSpace(target)) throw new Exception("默认界面启动路径为空。");
                ResolveOne(Path.GetDirectoryName(Path.GetFullPath(Path.Combine(dir, target))), result, seen, depth + 1);
            }
        }
        string current = Path.Combine(dir, "current.txt");
        if (File.Exists(current)) {
            string ptr = File.ReadAllText(current).Trim().Replace('\\', '/');
            if (ptr == ".") return;
            if (!Regex.IsMatch(ptr, @"^\.updates/versions/[0-9][0-9.\-]*$")) throw new Exception("版本指针无效，未修改任何配置。");
            ResolveOne(Path.GetFullPath(Path.Combine(dir, ptr)), result, seen, depth + 1);
        }
    }
    static void CheckRunning(List<string> dirs) {
        foreach (Process p in Process.GetProcesses()) {
            try {
                string n = p.ProcessName.ToLowerInvariant();
                if (n != "messager" && n != "cipherchat" && n != "cipherchat-launcher" && n != "cipherchat-ui-selector") continue;
                string path = p.MainModule.FileName;
                foreach (string dir in dirs)
                    if (path.StartsWith(dir.TrimEnd('\\') + "\\", StringComparison.OrdinalIgnoreCase))
                        throw new InvalidOperationException("请先退出此安装目录中的 CipherChat，再点击切换。程序不会强制关闭聊天窗口。");
            } catch (InvalidOperationException) { throw; }
            catch (System.ComponentModel.Win32Exception) { throw new Exception("无法确认 CipherChat 是否已退出，请关闭聊天程序后重试。"); }
            finally { p.Dispose(); }
        }
    }
    static void Replace(string path, byte[] data) {
        string temp = path + ".setup-" + Guid.NewGuid().ToString("N") + ".tmp";
        try {
            using (var s = new FileStream(temp, FileMode.CreateNew, FileAccess.Write, FileShare.None)) { s.Write(data, 0, data.Length); s.Flush(true); }
            File.Replace(temp, path, null);
        } finally { if (File.Exists(temp)) File.Delete(temp); }
    }
    public static string Apply(string selected) {
        var dirs = Resolve(selected); CheckRunning(dirs);
        var originals = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        var changes = new Dictionary<string, byte[]>(StringComparer.OrdinalIgnoreCase);
        foreach (string dir in dirs) {
            string path = Path.Combine(dir, "connection.json"); Safe(path);
            var c = Read(path); string host = Value(c, "host"), name = Value(c, "serverName");
            if (host != Name && host != Host) throw new Exception("此工具只迁移原官方服务器，检测到其他地址：" + host);
            if (name != Name || Value(c, "port") != "55220") throw new Exception("服务器证书名或端口与官方迁移配置不符，未修改。");
            string scope = Value(c, "accountScope");
            if (String.IsNullOrWhiteSpace(scope)) c["accountScope"] = host + ":55220/" + name;
            c["host"] = Host;
            originals[path] = File.ReadAllBytes(path);
            changes[path] = new UTF8Encoding(false).GetBytes(Json.Serialize(c));
            string update = Path.Combine(dir, "update-config.json");
            if (File.Exists(update)) {
                Safe(update); var u = Read(update);
                if (Value(u, "host") != Name && Value(u, "host") != Host) throw new Exception("更新服务器地址不是官方服务器，未修改：" + update);
                u["host"] = Host; u["port"] = 55220; u["server_name"] = Name;
                originals[update] = File.ReadAllBytes(update);
                changes[update] = new UTF8Encoding(false).GetBytes(Json.Serialize(u));
            }
        }
        // All files are validated before any write. Keep an exact-byte recovery copy.
        string backup = Path.Combine(Path.GetFullPath(selected), "server-switch-backups", DateTime.Now.ToString("yyyyMMdd-HHmmss") + "-" + Guid.NewGuid().ToString("N").Substring(0, 8));
        Safe(backup); Directory.CreateDirectory(backup);
        var journal = new List<Dictionary<string, object>>(); int i = 0;
        foreach (var pair in originals) {
            string file = (++i).ToString() + ".json"; File.WriteAllBytes(Path.Combine(backup, file), pair.Value);
            journal.Add(new Dictionary<string, object> { { "path", pair.Key }, { "file", file } });
        }
        File.WriteAllText(Path.Combine(backup, "journal.json"), Json.Serialize(journal), new UTF8Encoding(false));
        File.WriteAllText(Path.Combine(backup, "restore.cmd"), "@echo off\r\n\"" + Application.ExecutablePath + "\" --restore \"%~dp0\"\r\npause\r\n", Encoding.Default);
        var written = new List<string>();
        try {
            foreach (var pair in changes) { Replace(pair.Key, pair.Value); written.Add(pair.Key); }
        } catch (Exception e) {
            var failed = new List<string>();
            foreach (string path in written) try { Replace(path, originals[path]); } catch { failed.Add(path); }
            throw new Exception("切换失败：" + e.Message + "\r\n" + (failed.Count == 0 ? "已恢复所有已修改配置。" : "部分文件恢复失败，请使用备份手动恢复。") + "\r\n备份：" + backup);
        }
        return backup;
    }
    public static void Restore(string backup) {
        Safe(backup);
        var items = Json.Deserialize<List<Dictionary<string, object>>>(File.ReadAllText(Path.Combine(backup, "journal.json")));
        var dirs = new List<string>(); var originals = new Dictionary<string, byte[]>(); var restored = new Dictionary<string, byte[]>();
        foreach (var item in items) {
            string path = Path.GetFullPath(Value(item, "path")), file = Value(item, "file");
            if ((Path.GetFileName(path) != "connection.json" && Path.GetFileName(path) != "update-config.json") || !Regex.IsMatch(file, @"^[0-9]+\.json$")) throw new Exception("备份记录无效。");
            Safe(path); dirs.Add(Path.GetDirectoryName(path));
            originals[path] = File.ReadAllBytes(path); restored[path] = File.ReadAllBytes(Path.Combine(backup, file));
        }
        CheckRunning(dirs); var written = new List<string>();
        try { foreach (var item in restored) { Replace(item.Key, item.Value); written.Add(item.Key); } }
        catch { foreach (string path in written) Replace(path, originals[path]); throw; }
    }
    public static List<string> Discover() {
        var found = new List<string>();
        string home = Path.GetDirectoryName(Application.ExecutablePath);
        var candidates = new List<string> { home, Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData), "Programs", "CipherChat") };
        // Resolve existing user shortcuts without modifying them.
        foreach (string folder in new [] { Environment.GetFolderPath(Environment.SpecialFolder.DesktopDirectory), Environment.GetFolderPath(Environment.SpecialFolder.StartMenu) }) {
            try {
                Type t = Type.GetTypeFromProgID("WScript.Shell"); dynamic shell = Activator.CreateInstance(t);
                foreach (string file in Shortcuts(folder)) {
                    dynamic link = shell.CreateShortcut(file); string target = link.TargetPath;
                    if (!String.IsNullOrEmpty(target)) candidates.Add(Path.GetDirectoryName(target));
                }
            } catch { /* Manual folder selection remains available. */ }
        }
        foreach (string candidate in candidates) try { Resolve(candidate); if (!found.Contains(candidate)) found.Add(candidate); } catch { }
        return found;
    }
    static IEnumerable<string> Shortcuts(string root) {
        var files = new List<string>(); var pending = new Queue<string>(); pending.Enqueue(root); int count = 0;
        while (pending.Count > 0 && count++ < 500) {
            string dir = pending.Dequeue();
            try { files.AddRange(Directory.GetFiles(dir, "*CipherChat*.lnk", SearchOption.TopDirectoryOnly)); } catch { }
            try { foreach (string child in Directory.GetDirectories(dir)) if ((File.GetAttributes(child) & FileAttributes.ReparsePoint) == 0) pending.Enqueue(child); } catch { }
        }
        return files;
    }
}

class Wizard : Form {
    ComboBox folder = new ComboBox(); TextBox status = new TextBox(); Button apply = new Button(); Button browse = new Button(); Button restore = new Button();
    public Wizard() {
        Text = "CipherChat · 新服务器切换"; ClientSize = new Size(620, 390); Font = new Font("Microsoft YaHei UI", 10); StartPosition = FormStartPosition.CenterScreen; FormBorderStyle = FormBorderStyle.FixedDialog; MaximizeBox = false;
        var title = new Label { Text = "切换到新服务器", Font = new Font(Font.FontFamily, 19, FontStyle.Bold), Location = new Point(24, 20), Size = new Size(560, 40) };
        var info = new Label { Text = "新地址：8.160.172.25:55220\r\n保留账号身份和聊天文件，修改前自动备份。请先退出 CipherChat。", Location = new Point(26, 72), Size = new Size(570, 55) };
        folder.SetBounds(26, 143, 465, 30); folder.DropDownStyle = ComboBoxStyle.DropDown;
        browse.Text = "选择目录"; browse.SetBounds(500, 142, 94, 32);
        browse.Click += delegate { using (var d = new FolderBrowserDialog { Description = "选择包含 CipherChat 程序或启动器的安装目录", ShowNewFolderButton = false }) if (d.ShowDialog(this) == DialogResult.OK) folder.Text = d.SelectedPath; };
        status.SetBounds(26, 190, 568, 125); status.Multiline = true; status.ReadOnly = true; status.ScrollBars = ScrollBars.Vertical; status.BackColor = SystemColors.Window;
        status.Text = "选择已有安装目录，然后点击“备份并切换”。\r\n切换后重新打开原桌面快捷方式。如未自动填入登录信息，请手动输入原账号密码。\r\n此工具只修改连接配置，不安装或升级客户端。";
        apply.Text = "备份并切换"; apply.SetBounds(444, 336, 150, 36);
        restore.Text = "从备份恢复…"; restore.SetBounds(26, 336, 145, 36);
        apply.Click += delegate {
            if (MessageBox.Show(this, "将备份并修改所选安装的连接配置，切换至 8.160.172.25。\r\n继续吗？", "确认切换", MessageBoxButtons.OKCancel, MessageBoxIcon.Question) != DialogResult.OK) return;
            Run(delegate { string backup = Migration.Apply(folder.Text.Trim()); return "切换完成。请重新打开原桌面快捷方式并登录。\r\n备份目录：" + backup + "\r\n需要回退时，点击“从备份恢复”并选择此目录。"; });
        };
        restore.Click += delegate { using (var d = new FolderBrowserDialog { Description = "选择包含 journal.json 的备份目录", ShowNewFolderButton = false }) if (d.ShowDialog(this) == DialogResult.OK && MessageBox.Show(this, "恢复该备份中的连接配置？请先退出 CipherChat。", "确认恢复", MessageBoxButtons.OKCancel) == DialogResult.OK) Run(delegate { Migration.Restore(d.SelectedPath); return "连接配置已恢复。请重新打开 CipherChat。"; }); };
        Controls.AddRange(new Control[] { title, info, folder, browse, status, apply, restore });
        Shown += delegate { foreach (string p in Migration.Discover()) folder.Items.Add(p); if (folder.Items.Count > 0) folder.SelectedIndex = 0; };
    }
    void Run(Func<string> action) {
        apply.Enabled = browse.Enabled = restore.Enabled = false;
        try { status.Text = action(); MessageBox.Show(this, status.Text, "操作完成", MessageBoxButtons.OK, MessageBoxIcon.Information); }
        catch (Exception e) { status.Text = e.Message; MessageBox.Show(this, e.Message, "未完成", MessageBoxButtons.OK, MessageBoxIcon.Error); }
        finally { apply.Enabled = browse.Enabled = restore.Enabled = true; }
    }
}
static class Program {
    [STAThread] static int Main(string[] args) {
        Application.EnableVisualStyles(); Application.SetCompatibleTextRenderingDefault(false);
        if (args.Length >= 2) {
            try {
                string result;
                if (args[0] == "--apply") result = Migration.Apply(args[1]);
                else if (args[0] == "--restore") { Migration.Restore(args[1]); result = "restored"; }
                else if (args[0] == "--inspect") result = String.Join("\r\n", Migration.Resolve(args[1]));
                else if (args[0] == "--discover") result = String.Join("\r\n", Migration.Discover());
                else throw new Exception("未知参数");
                if (args.Length == 4 && args[2] == "--report") File.WriteAllText(args[3], result, new UTF8Encoding(false));
                else MessageBox.Show(result, "操作完成");
                return 0;
            } catch (Exception e) {
                if (args.Length == 4 && args[2] == "--report") File.WriteAllText(args[3], e.Message, new UTF8Encoding(false));
                else MessageBox.Show(e.Message, "未完成", MessageBoxButtons.OK, MessageBoxIcon.Error);
                return 1;
            }
        }
        Application.Run(new Wizard()); return 0;
    }
}
