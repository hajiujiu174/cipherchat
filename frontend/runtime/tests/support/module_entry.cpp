// Give each existing test main a private DLL boundary. Its doctest context,
// QApplication lifetime and assertions remain owned by that test module.
// 返回测试使用的ccfwmodulemain。
int ccfw_module_main(int argc, char** argv);
extern "C" /* 返回测试使用的ccfwrun测试。 */ __declspec(dllexport) int ccfw_run_test(int argc, char** argv) {
    return ccfw_module_main(argc, argv);
}
