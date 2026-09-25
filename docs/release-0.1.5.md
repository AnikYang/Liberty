# Liberty by Bada 0.1.5

关机计划现在可以直接查看和管理：

- 主菜单显示每日关机计划是否启用及计划数量。
- 管理窗口直接读取 Windows 任务计划，而不是只读取 Liberty 自身配置。
- 每条计划显示时间、启用状态、重复方式和下一次执行时间。
- 顶部显示系统任务状态、计划总数和最近的下一次执行。
- 添加计划会立即启用；选中计划后可以修改或删除，操作立即生效。
- “刷新”可导入从 Windows 任务计划程序进行的外部修改。
- “停用全部”保留时间列表；“启用／修复”重新创建任务并修复 Liberty.exe 路径。
- Windows 任务与 Liberty 配置不一致时，以系统中实际任务为准并自动修复显示状态。

原有一次性倒计时功能继续保留。电脑仍需处于开机、登录且未休眠状态；每日计划触发后启动 60 秒非强制关机倒计时。

Debug/Release 回归和真实窗口验收通过。现有 01:15、01:30 计划仅用于只读验证，没有被修改或删除。详细记录见 `docs/testing-0.1.5.md`。

English: The shutdown manager now reads the real Windows Task Scheduler task, shows each plan's state and next occurrence, and supports immediate add, edit, remove, refresh, disable-all and enable/repair actions. The main menu shows active plan count. Existing plans used for validation were not modified.
