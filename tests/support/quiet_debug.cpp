// OLY · tests —— 无窗调试引导：Debug CRT 的断言/崩溃改为 stderr 输出，禁止模态弹窗。
// 背景：/MDd 下 assert() 默认弹模态框、崩溃触发 WER 报告窗，会阻塞无人值守测试循环。
// 通过全局静态对象在 main 之前完成配置；仅 Windows（其余平台默认已无窗）。
#include <crtdbg.h>
#include <cstdlib>
#include <windows.h>

namespace {

struct QuietDebugBootstrap {
    QuietDebugBootstrap() {
        // CRT 三类报告（警告/错误/断言）→ stderr + 调试通道，不再弹窗
        _CrtSetReportMode(_CRT_WARN, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_WARN, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR);
        _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE | _CRTDBG_MODE_DEBUG);
        _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);

        // 调试器附加时，断言处主动中断以抓取调用栈；无调试器则继续 stderr 报告
        _CrtSetReportHook([](int, char*, int*) -> int {
            if (IsDebuggerPresent()) __debugbreak();
            return FALSE;   // FALSE = 继续默认报告流程
        });
        _set_error_mode(_OUT_TO_STDERR);

        // abort() 不再触发 WER 报告对话框
        _set_abort_behavior(0, _CALL_REPORTFAULT);

        // 崩溃（访问违例等）不再弹 WER 对话框；直接终止，由退出码/日志定位
        SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);

        // CRT 非法参数调用 → 静默返回（报告仍走上面的 stderr 通道）
        _set_invalid_parameter_handler(
            [](const wchar_t*, const wchar_t*, const wchar_t*, unsigned int, uintptr_t) noexcept {
                // 处理器签名要求返回 void；CRT 约定由调用方检查 errno
            });
    }
};

const QuietDebugBootstrap g_quietDebug;

}   // namespace
