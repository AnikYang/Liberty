# Liberty by Bada 0.1.3

定时关机新增“指定时间关机”：选择日期和 24 小时制的时刻，例如今天 **21:00** 或明天 **08:00**，点击“设定关机”即可。

- 原有倒计时关机保留，两种模式独立切换。
- 支持未来 7 天内的具体日期和时刻，拒绝已经过去的时间。
- 按秒计算 Windows 的剩余等待时间，避免整分钟取整带来的偏差。
- 显示计划日期、时间和倒计时，返回后重新打开会恢复原计划。
- 可以取消计划；确认日历日期不会顺带开始关机。
- Debug/Release 回归、真实 Windows 指定时间计划创建/取消和本机 21:00 界面流程测试通过。

下载 `Liberty.exe` 即可使用，校验值见 `SHA256SUMS.txt`。这是一次性关机计划。电脑须处于开机状态；未保存内容可能阻止关机。关闭 Liberty 不会取消已经提交给 Windows 的计划。修改系统时钟后请取消并重新设置。

English: Adds local date/time scheduling alongside the existing duration countdown. Select e.g. today 21:00 or tomorrow 08:00, view the target and countdown, reopen the dialog to restore it, or cancel. Past dates and targets more than seven days ahead are rejected. Calculation uses seconds rather than whole-minute rounding. Final Debug/Release regression and real OS schedule/cancel tests pass.
