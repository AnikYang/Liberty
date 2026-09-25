# Liberty by Bada 0.1.6

修复托盘主菜单不会自动关闭的问题：

- 主菜单成功打开后，点击桌面或切换到其他应用会自动收起。
- 使用“首次激活后才允许失焦关闭”的保护，避免历史上的菜单一闪而过。
- 点击菜单项目打开设置或关机计划管理时，主菜单正常关闭。
- 设置和关机计划管理是编辑窗口，不会因为临时切换应用而自动关闭；可使用返回、关闭或 Esc 结束。
- 点击底部关于区域会先关闭主菜单，再显示关于窗口。

Debug/Release 回归和真实桌面失焦测试通过。现有每日关机计划未被修改。

English: Fixes the tray popup remaining open after focus moves elsewhere. Auto-close is armed only after successful activation to avoid the historical flash-close race. Settings and plan-manager work windows remain open during editing.
