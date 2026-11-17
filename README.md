# PowerToggle

Windows 电源方案一键切换器。常驻系统托盘（通知区域），**单击图标即在两个预设电源方案之间切换**，支持空闲超时自动切换。纯 Win32 / C++，单文件、零依赖、约 56 KB。

## 功能

| 功能 | 说明 |
|---|---|
| 单图标 Toggle | 左键单击：A → B → A 循环，以系统真实状态为准 |
| 图标即状态 | 圆角方块 + 白色闪电造型，底色随当前方案变化（A/B 各用自定义颜色），不依赖文字；悬停提示显示方案真实名称，2 秒内同步外部改动 |
| 中性态 | 当前方案不属于 A/B 时显示灰色底，悬停显示真实方案名，单击切到 A |
| 异常态 | 配置的方案被删除时显示灰底 `!`，单击提示并打开设置 |
| 空闲自动切换 | 可配置阈值（1~480 分钟）、目标方案（A / B / 自定义）、冷却防抖 |
| 恢复后切回 | 可选：用户回来后自动切回空闲前的方案，默认关闭 |
| 开机自启 | 写入 `HKCU\...\Run`，无需管理员；路径变化自动校正 |
| 无黑框 | GUI 子系统 + `PowerSetActiveScheme` 直接调用，不产生子进程 |

## 编译

需要 MinGW-w64（`g++` + `windres`）或 MSVC（`cl` + `rc`）之一。

```powershell
# MinGW 需先把 bin 目录加入 PATH，例如：
$env:Path = "C:\toolchains\mingw64\bin;" + $env:Path

.\build.ps1           # 每次构建自动把 patch 版本 +1
.\tests\run_tests.ps1 # 单元测试（配置读写 / 电源枚举 / 空闲状态机 / 图标 / 托盘）
.\tests\smoke.ps1     # 运行时冒烟测试（会启动并结束程序）
```

### 版本与产物

版本号唯一来源是根目录的 `version.txt`。`build.ps1` 每次执行会：

1. 读取 `version.txt`，patch 位 +1，写回；
2. 生成 `src/version.h`（供 C++ 与资源脚本共用）；
3. 输出**带版本的产物**，并另存一份固定名便于日常使用：

```
build\PowerToggle-1.0.1.exe   ← 本版本专属产物，保留可作为回滚参照
build\PowerToggle.exe         ← 最新版本的副本
```

版本同时写入 exe 的版本资源，右键属性 → 详细信息可见；程序右键菜单第一行也显示当前版本，便于确认"跑的是不是最新那个"。

## 图标

**应用图标** `res/app.ico`（绿色圆角方块 + 白色闪电）由 `res/make_icon.ps1` 生成，含 16/24/32/48/64/128 六个尺寸，嵌入 exe 资源，用于资源管理器、任务栏、Alt-Tab 和设置窗口标题栏。需要改设计就改脚本里的绘制代码后重新执行：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File .\res\make_icon.ps1
```

> 注意：windres 读不了 PNG 压缩的 ico 条目，所以脚本把每个尺寸存成经典 32bpp DIB。

**托盘图标**是运行时用 GDI 现画的：圆角方块底色 + 白色闪电（与 app.ico 同形状），底色随当前方案变化（方案 A/B 用各自的颜色，中性/异常态用灰色），靠颜色和形状区分状态，不依赖文字。方案名通过鼠标悬停的 tooltip 展示，设置里的“悬停提示”就是 tooltip 里的简短别名。

## 使用

1. 双击 `PowerToggle.exe`，首次运行弹出配置窗口。
2. 选择**方案 A** 与**方案 B**（下拉列表来自系统实际方案），可自定义图标颜色；悬停提示直接显示方案名称。
3. 按需启用空闲自动切换、设置分钟数与目标方案、勾选开机自启。
4. 保存后托盘出现图标。若没看到，点通知区域的 `^` 把它拖到任务栏上（只需一次，之后 Windows 会记住）。
5. **左键单击图标**切换方案；**右键**打开菜单（设置 / 开机自启 / 退出）。

## 配置

配置文件位置：优先 `exe 同目录\config.ini`；若目录不可写（例如放在 `Program Files`），自动回退到 `%APPDATA%\PowerToggle\config.ini`。

文件为 UTF-16LE，可用记事本直接编辑；改完需重启程序生效。

```ini
[Schemes]
A={381b4222-f694-41f0-9685-ff5bb260df2e}   ; 平衡
B={8c5e7fda-e8bf-4a96-9a85-a6e23a8c635c}   ; 高性能

[Labels]
TextA=平衡
TextB=性能
ColorA=#2E7D32
ColorB=#C62828

[Idle]
Enabled=1
ThresholdSeconds=600
Target=A          ; A / B / custom
RevertOnResume=0
CooldownSeconds=5
```

## 空闲切换日志

每次**空闲自动切换**（含恢复操作后的自动回切）都会追加一行到配置目录下的 `PowerToggle-idle.log`：

```
2026-11-17 14:03:12 空闲切出 | 平衡 {381b4222-...} → 高性能 {8c5e7fda-...} | 空闲 600 秒
2026-11-17 14:30:05 恢复回切 | 高性能 {8c5e7fda-...} → 平衡 {381b4222-...} | 用户恢复操作
```

该日志与 `LogLevel` 无关，始终写入；且不会像 `PowerToggle.log` 那样在超过 64 KB 后被清空，适合长期留存。只有**空闲触发**的切换会记录，手动单击不写入，以免产生噪声。

## 目录结构

```
src/    ptdef.h util power config icon tray idle autostart settings main
res/    app.rc（对话框） app.manifest（DPI + comctl32 v6）
tests/  test_main.cpp  run_tests.ps1  smoke.ps1
docs/   需求文档.md
```

## 已知限制

- 新注册的托盘图标会被 Windows 折叠进 `^` 溢出区，这是系统默认行为，本工具不做注册表提升（按需求约定）；拖出一次后永久生效。
- 未签名的 exe 首次运行可能触发 SmartScreen 提示。
- 电源方案切换会引起亮度/风扇/频率短暂变化，因此"恢复后切回"默认关闭，并带 5 秒冷却。
