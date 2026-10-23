# 测试与 Qt 解耦

2026-09-17，范围为测试边界及构建依赖。生产界面的外观和业务协议未修改。

## 已确认的问题

核心服务原本不包含 Qt。耦合集中在 `tests/acceptance/gui_driver.cpp`：一个长流程同时检查注册、签收、文件落盘、账号恢复，以及 Tab 数量、中文页签、具体控件类型、父子结构、默认折叠和像素位置。M7-02 的账号迁移只能通过 GUI 驱动验收，M8 偏好测试直接寻找复选框。开启客户端构建还会连带要求 Qt Test。

## 调整后的边界

| 测试层 | 入口 | 验证内容 | Qt 依赖 |
|---|---|---|---|
| 业务 | `core-debug` / `desktop-core` | 密码协议、存储、消息、文件、群、真实业务队列上的迁移恢复 | 无 |
| GUI 交互 | `desktop-ui` | 真实输入点击、身份确认、显示签收/进度、会话筛选、设置保存、GUI 迁移 | 有，定位集中到适配器 |
| 当前布局 | `desktop-layout` | 默认收起、传输中保持收起、最小窗口滚动可达 | 有，可随设计调整 |
| 交付 | `desktop-delivery` | 已有便携包、文档及启动材料 | 检查已交付程序 |

`ccfw_client_runtime` 单独编译现有 `gui/business_worker.cpp`，不链接 Qt。暂保留源文件位置及 `ccfw::gui` 命名空间以兼容现有调用；独立性由构建目标和无窗口验收保证。Qt 窗口与测试复用同一份实现，没有复制服务逻辑。

`M7-02` 改用 `test_M7_02_runtime.cpp`：启动真实 TLS 服务及两个业务 worker，注册、核对双方真实公钥、互发签收、关闭 worker，再从新地址以原账号作用域登录，验证原身份、历史、信任、后续签收和单一 profile。原 Qt 迁移验收保留为 `M7-02-gui`，继续检查启动参数到 GUI/runtime 的连接。

`QtClientPage` 位于 `tests/ui/qt_client_page.*`。场景只使用“登录、选择会话、发送消息、选择文件、读取文件状态”等方法。适配器通过实际控件输入和点击操作，不直接调用 worker，也不通过读取内部业务缓存假装界面已经显示。控件名称、翻译文字和气泡结构仍由该适配器识别；彻底更换 Qt 界面时仍需更新适配器。

不再要求恰好三个 Tab、固定中文页签或固定导航对象名称。导航按能力控件所属页面定位，`UI-adapter-navigation` 会在真实窗口中改名、重排并增加页签后验证动作。文件在消息前后的展示顺序、身份核对、发送对象隔离与真实文件逐字节一致仍有断言。文件时间的业务持久化另外由无 Qt 的 M4-02 验证。

`M4-04-layout` 复用端到端场景并显式开启 `layout_checks`；未执行布局断言时对应证据字段保持 false，不伪报布局通过。GUI 偏好保存仍依赖 QSettings，归入 ui 层，不宣称已转成纯业务测试。

## 构建与维护

在 framework 目录运行：

```powershell
cmake --preset core-debug
cmake --build --preset core-debug -j 2
ctest --preset core-debug

cmake --preset desktop-debug
cmake --build --preset desktop-debug -j 2
ctest --preset desktop-ui
ctest --preset desktop-layout
ctest --preset desktop-delivery
.\scripts\verify.ps1 -AuditOnly
```

仅构建客户端、禁用测试依赖：

```powershell
cmake --preset desktop-debug -DCCFW_BUILD_GUI_TESTS=OFF -DCCFW_BUILD_DELIVERY_TESTS=OFF
cmake --build --preset desktop-debug --target ccfw-desktop
```

重新执行 `cmake --preset desktop-debug` 恢复完整桌面测试配置。`BUILD_TESTING=OFF` 仍可关闭全部测试。构建预设将本机 MinGW 放在 PATH 前面，避免 Qt 目录中的同名运行库干扰编译器。

新增业务行为优先写 core 测试；只属于显示和交互的条件写 ui；折叠、尺寸和版式写 layout。`scripts/verify.ps1` 同时检查 worker 不包含 Qt，以及场景不重新引入控件查找。

## 本次验证

本次 core-debug 与 desktop-debug 构建均成功，分层运行覆盖全部 29 个登记用例：

| 验证 | 结果 | 耗时 | 记录（相对 framework） |
|---|---|---|---|
| 无 Qt 业务 | 23/23 通过，含 M7-02 runtime 迁移 | 51.37 秒 | `artifacts/test-decoupling-20260917/core-tests.log` |
| Qt 交互 | 4/4 通过，含改名/重排/增加页签、双窗口业务、GUI 迁移和偏好 | 18.03 秒 | `artifacts/test-decoupling-20260917/ui-tests-fixed.log` |
| 当前布局 | 1/1 通过，保留完整可见断言 | 9.45 秒 | `artifacts/test-decoupling-20260917/layout-tests-fixed.log` |
| 已有交付材料 | M6-01，1/1 通过 | 6.18 秒 | `artifacts/test-decoupling-20260917/delivery-tests.log` |

依赖检查：71 条核心编译命令均无 Qt 路径/模块引用；M7-02 runtime 可执行文件的导入表没有 Qt DLL。另在 `build/framework-ui-no-tests` 配置 Qt Test 禁用、GUI 开启的构建成功；该额外目录只验证配置依赖，没有重复完整编译。检查记录：`artifacts/test-decoupling-20260917/dependency-check.json`。

首次 GUI 验证发现复选框控件中心落在非点击区域，适配器已改为聚焦后按空格；首次布局验证发现滚动后的卡片顶部超出 6 像素，适配器改为对当前卡片再次滚动定位，完整可见断言保持。失败日志与修复后的成功日志均保留。最小窗口截图已查看，文件卡片完整显示。

这些结果验证源代码构建、真实本机测试窗口和已有交付材料；未重新制作发布包、未重跑公网或多实体设备测试。旧任务及旧交付结果保留。

## M10 增量验收（2026-10-08）

无 Qt 的独立核心构建29项通过，最终调整登录恢复顺序后另复核 M7-02 runtime。核心新增测试覆盖身份卡解析与实际密钥比较、交付恢复及旧端兼容、真实 TLS 下载超时与缓存，不通过 GUI 对象检查代替业务结果。核心依赖检查包含102条编译命令及 runtime 导入表。

桌面最终15项结果来自两个批次：第15批14项通过，首次 Qt 窗口曝光等待不足使热切换项超时；第16批先等待真实窗口曝光，再验证完整开场、共享登录、草稿和文件热切换，原时间、可见及文件字节断言保留。不得把这两个批次拼成一次全套成功 JUnit。

身份卡回归向真实卡片连续发送20次相同身份事件，检查卡片对象、已输入指纹及核对状态保持；变更公钥后旧确认禁用。端到端驱动还检查同一身份无需反复确认。工作台覆盖三个窗口尺寸、两种主题和1/1.25/1.5缩放，读取实际控件边界及渲染像素；心跳阈值使用同机旧场景基线，包含首次三维资源加载。

最终两个 ZIP 在独立目录解压，移除开发工具的 Qt PATH 后核对清单并启动两套界面；六个本机故障场景及演示宿主通过，再通过专用本机 TLS 服务执行真实旧版到新版激活、重启和回退，核对文件及隔离资料标记。这里验证本机和独立包行为，另一台电脑的现场下载体验尚未复核。完整日志和保留的失败批次见 [本地交付记录](../../docs/features-20261008.md)，公共服务器未更新。

最终本地修订包为2026.10.8.2，验收使用包内真实的 `start-remote.cmd` 与 `start-demo.cmd`，记录为 `test-tmp/features-package-20261008-r4/result.json`。启动探测按实际可见窗口标题匹配并在有限时间内检查响应，冷启动等待失败记录保留。版本元数据调整后的三个受影响核心验收另行通过；这三个用例与原29项重合，不重复计数。

## 移除答辩演示（2026-10-11）

原演示测试及专用工具已删除，CMake不再登记M10-demo-process、M10-demo-controls、M10-demo-ui。工作台与正常热切换验收独立保留；旧端兼容测试改用tests/compatibility/process_io.hpp的通用文件和进程工具，不依赖演示代码。原测试日志、旧包与原DOCX保留，不能用它们代替当前版本结果。最新结果见 [演示移除记录](../../docs/remove-demo-20261011.md)。新包继续只保存在本地。
