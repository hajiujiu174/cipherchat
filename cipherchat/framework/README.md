# CipherChat 业务实现与验收入口

当前正式界面是 `gui/rhine_native` 的 Qt 复刻版，支持与 Codex 同进程热切换；网页实现及专用桥接源码已归档。三段背景音效已纳入原生资源，不再依赖网页目录。构建 `cmake --preset verification`、`cmake --build --preset verification`；未保存界面偏好时默认 Qt Rhine。

日常回归执行 `scripts/test.ps1 -Suite core` 或 `-Suite ui`。固定 verification 构建和共享 `build/test-host/cipherchat-tests.exe` 减少网络授权对象。测试模块在各自新进程内运行，保留核心无 Qt、GUI 独立及跨进程恢复验证。原测试目标名现在构建 DLL 模块，使用 CTest 或共享入口运行，不能直接双击 DLL。专项更新/交付测试仍验证真实生产程序。

M8-11：Qt 聊天、群聊和文件传输移到左侧模型上方的磨砂玻璃工作面板，修复消息行重叠，输入区固定在底部。2026.10.5.5 本机验收与安装见 [说明](../docs/glass-workspace-20261005.md)。

M8-10：Qt Rhine 与 Codex 同进程热切换，保留登录、草稿及进行中的文件传输，并恢复完整更新模块。2026.10.5.3 恢复 Qt 完整开场动画，登录跳转等待动画结束；[实现与验收](../docs/hot-switch-20261005.md)。

2026.10.5.4 已发布服务器，使用兼容旧更新器的 177 文件包，实际旧版下载、激活、启动与回退验证通过；[发布记录](../docs/animation-publication-20261005.md)。

M8-09：RhineLabUI 原生移植。第三套 Rhine 界面使用 Qt Quick 3D 原始档案模型、暖灰终端样式和编号导航，保留三套设计切换、独立主题色与胶囊深浅开关。桌面预设使用 Qt 配套 MinGW 13.1 和独立构建目录。入口与构建说明见 [Rhine 界面](../docs/rhine-ui-20260927.md)。

M9-02：联系人采用申请同意制；提供好友、临时会话、陌生人分页查询和好友申请栏目。中心持久保存关系与待处理申请，新客户端登录不再拉全量用户。[实现、发布与验证](../docs/contacts-20260920.md)。

M9-01：中心服务器版本检查、256 KiB 内容块增量下载、独立目录激活及回退；GUI 使用独立更新进程，业务线程接口不变。构建生成 `cipherchat-updater.exe` 与稳定启动器，验收为 M9-01 和 UI-updates。[发布与验证说明](../docs/updates-20260919.md)。

M8-08 后续：深浅色下拉框改为带框内文字的胶囊开关，鼠标单击或空格切换。[说明及验证](../docs/appearance-switch-20260919.md)。

追加 M8-08：两套设计的深浅色模式、偏好恢复和暗色控件。当前 GUI/layout 共八项；详见 [深浅色说明](../docs/appearance-20260919.md)。

追加 M8-07：参考图取色面板、GLM / Codex 各自主题色和收藏。新增 `UI-theme-colors`，当前 GUI/layout 共七项；详见 [主题色说明](../docs/theme-colors-20260919.md)。

2026-09-19：新增 GLM / Codex 双界面，窗口右上角即时切换并记住偏好。运行项目根 `启动CipherChat-双界面.bat`；实现与验证见 [双界面说明](../docs/dual-design-20260919.md)。

M0—M6 的 22 项任务已完成。正式客户端由 `gui/main.cpp` 创建 MainWindow 和 BusinessWorker，业务服务全部接入 ClientApplication；界面包含账号、聊天、群聊三页，文件选择、发送和会话传输进度已合并到聊天页。启动方法和完整边界见项目根 `README.md`，验收汇总见 `../docs/verification.md`。

追加 M7-01 将文件传输替换为 16 块滑动窗口、累计确认与 CBOR，删除正式客户端的旧文件传输路径。双方须升级；旧账号与聊天记录兼容，旧版未完成文件需要重新发送。实现、性能对照和新发布包见 `../docs/file-transfer-upgrade.md`。

## 代码边界

- `include/ccfw`：公共接口、DTO 和显式 Result 错误；核心无 Qt 与 server 包含。
- `src`：账号、信任与会话、文本、文件、群、传输、DPAPI 快照及消息编排。
- `gui`：Qt 控件；其中 `business_worker.*` 独立编译为无 Qt 的 `ccfw_client_runtime`，承载命令与事件队列。
- `tests/acceptance`：业务验收与端到端场景；业务用例直接调用服务或无 Qt runtime。
- `tests/ui`：Qt 测试适配器，集中控件定位、实际点击、展示状态读取和布局检查。界面改版时在这里适配。
- `tasks.json` 和 `tasks`：29 项任务状态及原始/最终证据，包括原 M0—M6、M7 文件升级/服务器迁移及 M8 混合传输/聊天文件时间线/测试解耦。任务卡保留实施步骤以便维护。
- `docs/baseline.md`、`artifacts/verification.md`：接手时的历史记录，不能当作当前状态。

原 `common/crypto`、X3DH、Double Ratchet 和文件块函数保持单一源码。正式客户端不链接 server；旧 `client/core/client.cpp` 只保留供 v1 回归，不能与新组合根同时处理同一连接。

## 构建与测试

在本目录执行，先让已有 MinGW、Qt 工具目录进入当前进程 PATH：

```powershell
$env:PATH = 'D:\msys64\mingw64\bin;C:\Qt\6.11.2\mingw_64\bin;' + $env:PATH
cmake --preset core-debug
cmake --build --preset core-debug -j 3
ctest --preset core-debug

cmake --preset desktop-debug
cmake --build --preset desktop-debug -j 3
ctest --preset desktop-ui        # GUI 交互与设置
ctest --preset desktop-layout    # 当前版式、折叠和最小窗口
ctest --preset desktop-delivery  # 已有交付包及文档材料
# ctest --preset desktop-debug   # 全部测试
.\scripts\verify.ps1 -AuditOnly
```

`core-debug` 为不查找、不链接 Qt 的 23 项测试，包含真实业务线程上的账号迁移；`implementation-debug` 保留为兼容预设。`desktop-debug` 注册 29 项：23 项 core、4 项 ui、1 项 layout、1 项 delivery。也可通过 `ctest --preset desktop-core` 在桌面构建中单独跑业务。每项测试使用其 CMake 声明的超时。

`CCFW_BUILD_GUI`、`CCFW_BUILD_GUI_TESTS`、`CCFW_BUILD_DELIVERY_TESTS` 分别控制客户端、GUI 测试与交付检查。只构建客户端时可显式关闭后两项，不要求 Qt Test。M6-01 仍需要已有交付包、最终 DOCX 和 QA 记录；开发测试通过不代表重新打包或完成交付。分层原则和变更证据见 [测试与 Qt 解耦](docs/testing-boundaries.md)。

M0—M6 历史完整桌面回归为 22/22 通过，42.88 秒；独立核心 20/20，34.21 秒；根项目 unit 通过 50 个 doctest case。GUI 归档样本最大心跳间隔为 Alice 55 ms、Bob 48 ms。本次追加验证单列于 `../docs/file-transfer-upgrade.md`，不覆盖历史证据。

## 维护规则

继续使用 `docs/contracts.md` 中的版本、线程与事务约定；变更接口时同步修改调用方和验收。`scripts/task.ps1` 当前没有待办，新增需求应明确添加任务及验收，不复活已经完成的骨架占位。

正式路径为 TLS 1.3 验证加 v2 协商；服务端看得到账号、路由和群元数据，注册口令在验证的 TLS 内提交。自定义课程协议不与 Signal 互通，DPAPI 不防护已控制当前 Windows 用户的进程。更多边界见项目根 README。

追加 M8-02：聊天页复用当前联系人发送文件，列表按联系人筛选，保留恢复、取消和直连偏好。验收与新包见 `../docs/chat-files-20260916.md`。

M8-02 后续调整：文件功能合并为默认收起的“文件传输”框，标题持续显示当前会话的文件数量和状态；消息输入与发送始终在框外。传输中可收起，进度刷新不自动展开。最新交付及验证见 `../docs/collapsible-files-20260916.md`。

追加 M8-03：账号页精简为登录操作；连接信息收进高级设置，身份指纹不再常驻主界面，安全详情按需查看。原安全机制及身份数据保留。验收见 `../docs/clean-ui-20260916.md`。

M8-03 后续调整：高级设置也不再显示证书路径、证书浏览按钮或证书主机名，认证直接使用启动配置。新版及本轮 3/3 GUI 回归和包验证见 `../docs/certificate-ui-20260916.md`。

M8-04：文件卡片按实际发送时间直接穿插在聊天文字之间，替代独立文件表格。时间随加密元数据及本地快照保存，重登录不改变位置；旧记录时间未记录则明确标注。附件工具仍可折叠。完整 26/26 回归及新包见 `../docs/timeline-files-20260916.md`。
