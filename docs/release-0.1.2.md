# Liberty by Bada 0.1.2 RC1

新增定时关机：15/30 分钟、1/2 小时，以及自定义 1–10080 分钟；显示倒计时，可随时取消，返回不退出软件。

新增“防止自动锁屏”：在已解锁会话中维持活动，默认关闭，手动锁定仍然有效，不修改 Windows 锁屏策略。

修复菜单首次打开时勾选状态未显示、原生开关被父窗口绘制覆盖、先松开 Cmd 时映射按键未正确释放，以及保存截图时可能清空后来复制内容的问题。增加菜单键盘导航和自动回归测试。

已完成 Debug/Release 构建、自动回归测试、Windows 调试事件监测，以及真实的“创建 60 分钟关机计划 → 立即取消 → 系统确认无待执行关机”测试。没有实际执行关机。

实际桌面已验证非法输入提示、120 分钟计时、返回、重新打开倒计时及取消关机。最终 Debug 回归通过；最后一轮 Release 重编译后被本机 Windows 应用程序控制签名策略拦截，因此保留候选版本，尚未完成最终 Release 本机再验收。详细范围见仓库的 `docs/debug-0.1.2.md`。

下载 `Liberty.exe` 即可运行；`SHA256SUMS.txt` 对应本次 Release 中的文件。计时后关闭窗口或退出 Liberty 不会取消 Windows 的关机计划，需要点击“取消关机”。有未保存内容的应用不会被强制关闭。

English: Adds a native shutdown timer and optional automatic-lock prevention. Fixes checkbox initialization/painting, key release ordering, and a clipboard race. Real desktop schedule/back/reopen/cancel and invalid-input flows were tested. Final Debug regression tests pass. Local Windows application-control signing policy blocks the final Release rebuild, so final Release desktop acceptance remains pending; this is a release candidate.
