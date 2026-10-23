# 初始基线核验（2026-09-14，历史记录）

本文保留接手时的代码状态和原始失败，不表示最终交付状态。后续修复、已完成任务与最终验收见 `../tasks.json`、项目根 `docs/verification.md` 和 `README.md`。

## 范围

读取用户给定的 CipherChat 实施方案、现有 CMake、核心头/源文件与测试。没有修改原源码、原 CMake、原方案文件或 Word 文档。framework 是新增目录，构建和测试临时数据都位于其 build 下。

## 原代码现状

| 能力 | 已有内容 | 本次可确认的限制 |
|---|---|---|
| M0 | vendored json/doctest、frame、Base64、CMake | 根 GUI option 目前未接入 Qt target |
| M1 | EVP 原语、X3DH、DoubleRatchet、测试 | decrypt 在验签前改变状态，需补事务语义测试 |
| M2 | TLS、SQLite、认证、prekey、路由 | 客户端 TLS 默认 SSL_VERIFY_NONE；离线队列 drain 即删 |
| M3 | NetClient、ClientCore、headless harness | client.cpp 引用 server/auth；身份只留内存，重启加载未接 |
| M4 | E2E 文本、file_meta、分块重传 | openChunk 错误拒绝 0..15 字节密文；全文件驻内存 |
| M5 | 群创建、在线、输入事件 | 公开 sendGroupText 不存在；没有完整跨进程恢复 |
| M6 | docs/scripts 目录 | 本次读取时为空，不能据方案描述宣称已交付 |

附加边界：现文件密钥为随机 32 字节后经会话传递，与方案“从会话密钥派生”不同。框架选择保留随机文件密钥经 E2E 元数据传递的现有路线，并要求文档明确这项设计选择，不读取或复用 ratchet 根密钥。

## 原项目独立构建与测试

```powershell
cmake -S 'D:\Work\WORKDONE\cipherchat' -B 'D:\Work\WORKDONE\cipherchat\framework\build\baseline' -G Ninja -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DCIPHERCHAT_BUILD_GUI=OFF
cmake --build 'D:\Work\WORKDONE\cipherchat\framework\build\baseline' --target cipherchat-tests -j 4
ctest --test-dir 'D:\Work\WORKDONE\cipherchat\framework\build\baseline' --output-on-failure --timeout 45
```

配置与编译成功。本机识别 GNU 16.2.0、OpenSSL 3.6.4、SQLite 3.53.4。
完整测试本次运行 12.48 秒：**43 cases，42 passed，1 failed；789 assertions，788 passed，1 failed**。

失败：
- `tests/test_integration.cpp:224` 的 E2E-2 离线消息用例。
- `tests/test_integration.cpp:235`：`alice.waitRegister(8000, &server)` 返回 false。
- 失败发生在注册等待阶段，不能据此断言离线路由本身就是根因；交 M0-02 定位。

编译警告：SQLite::SQLite3 target 被标记弃用；Base64 有 signedness 比较；测试用 `'长'` 多字符常量溢出。这些都是原代码警告，不是框架新增问题。

详细测试输出：`../build/baseline/Testing/Temporary/LastTest.log`。原 build/core 的旧成功日志只显示 14 个测试，本次不据其声明完整业务通过。

## 新框架验证

- core-debug：配置、编译链接成功；framework-contracts 通过。
- desktop-debug：配置、编译链接成功，生成 Qt 窗口程序；最终 preset 同时复用原算法。
- implementation-debug：配置、编译和契约测试通过；完整结果见 ../artifacts/verification.md。
- 契约测试仅验证 Result、初始 DTO 和依赖组装/关闭，不把空实现计为业务通过。
- 未启动 Qt 窗口进行视觉或实际聊天验收；没有验证生产安全或 Signal 互操作。
- 21 张后续任务卡仍为 todo；M0-01 是本次完成的框架任务。
