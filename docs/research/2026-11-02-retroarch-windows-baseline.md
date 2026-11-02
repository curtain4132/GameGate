# RetroArch Windows 原版构建与启动验证

- 日期：2026-11-02
- 范围：构建未修改的上游源码，验证版本查询、菜单启动及动态核心加载。
- 结论：Windows x64 原版构建成功；菜单及 2048 核心启动冒烟测试通过。尚未完成实际游玩验收，也未决定 GameGate 采用 Fork 或独立宿主。

## 1. 基线与目录

- 上游源码：`D:\Project\XJ\Game-Gate\Code_Source\RetroArch`
- 产品目录：`D:\Project\XJ\Game-Gate\Code_Source\GameGate`
- 分支：`master`
- 提交：`11e8931021eb6b7fa69c04f52dd5d385841fdbbe`
- 构建前后上游工作区均干净，没有源码修改。
- GameGate 当前未初始化 Git 仓库；新增的 [.gitignore](../../.gitignore) 已忽略 `artifacts/`。
- 程序报告版本：`1.22.2 Nov 2 2026`。这是当前 master 提交构建结果，不表示检出了稳定发布标签。

全部构建产物、下载核心和运行数据放在 [artifacts/retroarch-baseline](../../artifacts/retroarch-baseline/)，不放入上游源码目录。

## 2. 构建环境

| 项目 | 实测信息 |
|---|---|
| 工具 | Visual Studio 2022 Build Tools 17.14.35 |
| MSVC | 14.44.35207，程序报告编译器 1944 |
| 工具集 | v143 |
| Windows SDK | 10.0.26100.0 |
| 配置 | Release / x64，普通配置，不启用 Qt／Cg 附加配置 |
| CPU | AMD Ryzen 7 6800H |
| 图形 | AMD Radeon Graphics，运行日志报告 OpenGL 4.6 |
| 音频 | WASAPI 共享模式初始化成功，48000 Hz |

第一次使用 Community 实例构建失败，错误为 MSB4019，缺少 `Microsoft.Cpp.Default.props`。通过 vswhere 的 C++ 组件筛选确认独立 Build Tools 安装了所需工具，改用该实例后构建成功，没有修改项目文件或安装额外工具链。

## 3. 可复现的构建命令

在 PowerShell 中执行：

```powershell
$workspace = 'D:\Project\XJ\Game-Gate\Code_Source'
$artifact = "$workspace\GameGate\artifacts\retroarch-baseline"
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$project = "$workspace\RetroArch\pkg\msvc\msvc-2022\RetroArch-msvc2022.vcxproj"

& $msbuild $project -t:Build -p:Configuration=Release -p:Platform=x64 `
  "-p:OutDir=$artifact\bin\" "-p:IntDir=$artifact\obj\" `
  -m:2 -nologo -verbosity:minimal -fileLogger `
  "-fileLoggerParameters:LogFile=$artifact\build-buildtools.log;Verbosity=normal"
```

结果：构建日志明确显示成功，耗时 `00:00:30.20`，0 错误、2 警告。两条均为 `libretro-common/string/rstrtod.c:1187` 的 C4756 常量运算溢出警告；本轮保留原样，未判断其所有潜在影响。

产物：[RetroArch-msvc2022.exe](../../artifacts/retroarch-baseline/bin/RetroArch-msvc2022.exe)。

原日志使用本机中文编码，已转换一份 UTF-8 副本便于阅读：[构建日志](../../artifacts/retroarch-baseline/build-buildtools.utf8.log)。

执行器注意事项：MSBuild 主进程结束、日志写出成功结果后，后台任务仍因编译辅助进程保持输出句柄而未结束。已取消该无实际构建工作的后台任务。因此不能用该任务最终的 killed 状态表示编译失败或声称获得了其正常退出码；构建成功依据是完整日志与后续产物运行。另一次从 MSYS bash 直接启动 exe 返回 Permission denied，改由原生 PowerShell 调用同一产物成功；该现象未完成根因核查，不作为程序缺陷结论。

## 4. 启动测试

独立配置：[smoke.cfg](../../artifacts/retroarch-baseline/smoke.cfg)。使用 RGUI、OpenGL、WASAPI，关闭退出时保存配置。未覆盖用户现有 RetroArch 配置。

### 菜单测试

```powershell
$root = 'D:\Project\XJ\Game-Gate\Code_Source\GameGate\artifacts\retroarch-baseline'
Set-Location $root
& "$root\bin\RetroArch-msvc2022.exe" --verbose `
  --config "$root\smoke.cfg" --log-file "$root\menu-smoke.log" `
  --menu --max-frames=120
```

结果：退出码 0。日志显示 OpenGL 上下文、菜单显示驱动和 WASAPI 初始化成功，随后正常卸载。

证据：[菜单日志](../../artifacts/retroarch-baseline/menu-smoke.log)。

### 2048 动态核心测试

核心来源：[Libretro 官方 Buildbot 的 Windows x64 nightly 文件](https://buildbot.libretro.com/nightly/windows/x86_64/latest/2048_libretro.dll.zip)。该地址内容可变，因此记录下载文件的 SHA-256。

2048 是无需外部 ROM 的游戏核心，用来验证动态核心接口，不是 FC／GBA 等硬件模拟器兼容性测试。本轮下载用于本机验证，尚未完成再分发许可审查。

```powershell
$root = 'D:\Project\XJ\Game-Gate\Code_Source\GameGate\artifacts\retroarch-baseline'
Set-Location $root
& "$root\bin\RetroArch-msvc2022.exe" --verbose `
  --config "$root\smoke.cfg" --log-file "$root\2048-smoke.log" `
  -L "$root\cores\2048_libretro.dll" --max-frames=180 `
  --max-frames-ss --max-frames-ss-path "$root\2048-smoke.png"
```

结果：退出码 0。成功加载 DLL，协商 Libretro API、像素格式和帧缓冲，输出 2048 标题画面；退出时日志显示 SRAM 写入成功、核心正常卸载。

- [核心运行日志](../../artifacts/retroarch-baseline/2048-smoke.log)
- [输出截图](../../artifacts/retroarch-baseline/2048-smoke.png)

截图停留在 `PRESS START` 标题页，未注入或手动执行游戏输入。写入 SRAM 不代表已经验证读取恢复或即时存档一致性。

### 已知提示与验收边界

- XInput 手柄驱动初始化失败，自动回退到 DirectInput。尚未确认具体原因，也未验证实体手柄。
- 使用默认 GLSL 着色器，未验证外部滤镜资源。
- 2048 核心报告采样率 0，宿主回退到输出采样率 48000 Hz。仅能确认音频设备初始化，不证明游戏声音正常。
- 尚未验证键盘／触控／手柄操作、实际游戏过程、暂停菜单交互、读档恢复和长时间稳定性。
- 尚未验证老主机核心、ROM 文件导入、BIOS、第三方游戏源和自定义 UI。
- 其他五端未进行构建或运行验证。

## 5. 文件指纹

SHA-256 用于固定本轮产物身份，不替代发布者身份验证。

| 文件 | SHA-256 |
|---|---|
| [构建产物](../../artifacts/retroarch-baseline/bin/RetroArch-msvc2022.exe) | `83F7305D4306595F1280CF4DE44A177297199B7DEC502256193E571FAA8FB810` |
| [核心压缩包](../../artifacts/retroarch-baseline/2048_libretro.dll.zip) | `63EA358C560B33814A487E20FF7EDD09C32CFF8EC217C896806DB1CAC3F26500` |
| [核心 DLL](../../artifacts/retroarch-baseline/cores/2048_libretro.dll) | `CEF5AECD9A666310781B9FE57A97DA613028BFA01F3A414A1F7BBC2A4CAE170B` |

## 6. 工作区权限处理

初始受限命令在工作区授权阶段返回 `SetNamedSecurityInfoW failed (Win32 5): grantWrite`，实际工具链命令未运行。

经一次获准的不受限诊断，确认工作区根目录缺少当前用户有效 WRITE_OWNER。诊断脚本仅为根目录添加当前用户完全控制，并回读确认 WRITE_DAC／WRITE_OWNER 均为 true；未修改所有者或源码内容，未发现需移除的应用包权限项。

随后用户将会话切换为完全权限。本轮后续构建在完全权限模式下完成，因此未证明原沙箱模式重试已经成功。

报告与备份位于工作区的 [permission-recovery](../../../permission-recovery/)。恢复原根目录权限的命令：

```powershell
pwsh -NoProfile -File 'D:\Project\XJ\Game-Gate\Code_Source\permission-recovery\acl-backup-7227fa3c49ab4d1daca364a4382317f4.json.ps1' -Path 'D:\Project\XJ\Game-Gate\Code_Source' -AllowRoot 'D:\Project\XJ\Game-Gate\Code_Source' -Restore 'D:\Project\XJ\Game-Gate\Code_Source\permission-recovery\acl-backup-7227fa3c49ab4d1daca364a4382317f4.json'
```

本轮未执行恢复。诊断报告：[权限报告](../../../permission-recovery/acl-report-3c0ec292ce57424eacea469f916ac0fa.jsonl)。

## 7. 下一阶段建议

1. 选择一个老主机核心和可合法分发的测试 ROM，验证输入、音频、暂停、存档与恢复。
2. 只读梳理游戏加载、菜单切换、核心生命周期和配置边界，评估自定义 GameGate 游戏库的接入点。
3. 比较 RetroArch 二次开发与自建 C／C++ Libretro 宿主的成本，再确定产品设计。
4. 继续遵守既定方向：仅 App 内运行，不考虑 Rust；不把当前启动冒烟测试当作产品实现完成。

本轮由主 Agent 执行原版构建和测试，仅新增验证配置、忽略规则与本文，没有实现或修复业务源码，不涉及双模型代码实现／审查流程。
