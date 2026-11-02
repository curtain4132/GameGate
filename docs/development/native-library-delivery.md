# GG-001 原生游戏库首批交付说明

日期：2026-11-02。分支：`feat/native-game-library`。源码基线：`9fa533cecd2ffddcfc4fa29fe07fea320577442f`。

## 本批行为

- 原生主菜单新增独立“我的游戏库 / My Game Library”，原 Playlists 保留。
- 游戏库将原生导入入口置于前部，展示现有游戏集合，并按原设置展示历史与收藏。
- 原 Import 入口放在哪个页面，不影响库内导入入口；kiosk 模式不显示导入，集合、历史和收藏分别遵守自己的可见性配置。
- 不新增总库文件，不复制历史或收藏，不修改 playlist、扫描任务、内容加载或核心运行循环。
- 提供英文标题／说明和独立维护的简体中文资源；其他语言的新文案缺译时回退英文。
- 集合中的核心选择、内容启动、关闭确认沿用原生行为。正常配置下关闭内容返回该游戏条目上下文，后退返回原集合；不会强制跳到游戏库根，也不覆盖用户显式启用的关闭后退出设置。

本批不包含在线游戏源、核心下载器的新实现、游戏元数据库、新封面首页或账户能力。

## 入口及试用

独立验收构建位于 [测试程序（仅本地存在，不随仓库分发）](../../artifacts/lead-acceptance/build/bin/RetroArch-msvc2022.exe)。这些文件在 artifacts 下，不提交源码，也不是正式发行包。

运行后从主菜单进入“我的游戏库”，选择“导入内容”，沿用现有目录／文件扫描或内容扫描；已存在的用户集合会在游戏库内显示。

简体中文 RGUI 需要原生 CJK 位图字体。缺少相应资源时，上游会把整个 RGUI 语言退回英文，而不只是新文案。验收使用官方 retroarch-assets 仓库的四个字体，放在 `artifacts/lead-acceptance/assets/rgui/font/`，并通过独立配置指定 assets_directory。正式发行仍需明确资源打包与许可方案。

## 独立验收结果

主 Agent 在 Codex 停止写入后，实际执行以下检查。

| 检查 | 结果与边界 |
|---|---|
| Windows Release x64 全新输出目录构建 | 退出码 0，MSVC v143；没有依赖实现者已有 exe 代替构建 |
| 真实前端菜单 harness 重新构建并运行 | 退出码 0，0 failures；测试执行真实菜单与消息逻辑，不是字符串搜索替代 |
| label 生成器重跑 | 退出码 0，生成文件 SHA-256 不变 |
| 翻译转换保护与 sublabel checker selftest | 退出码 0 |
| packed/format checker selftest 与全表校验 | 退出码均 0；50 个 header，4816 个英文消息；主 Agent 重新运行了原工具的已编译程序 |
| RGUI 英文 | 游戏库、导入、集合与启动路径通过 |
| RGUI 简体中文 | 补齐字体后，中文标题、导入导航通过 |
| RGUI 德文 | 原有内容德文，新功能缺译回退英文，导入导航通过 |
| RGUI kiosk | 游戏库内不显示导入，正常退出 |
| Ozone／XMB／MaterialUI（glui） | 按各驱动配置启动，游戏库→导入→返回的真实菜单路由通过；未进行完整图标和布局视觉验收 |
| 从集合运行与关闭 | 2048 核心实际加载；关闭二次确认后状态为 CONTENTLESS；回到条目页，再后退回原 Acceptance 集合 |
| 未指定核心 | 打开原生 Suggested cores 页面；本测试目录没有匹配核心信息，显示 No Cores Available 及原有获取入口 |
| 缺失文件／缺失核心 | 没有启动游戏，菜单仍可用，日志记录 Failed to load from playlist；不是新增的专门错误 UX |

核心测试使用已有 2048 动态核心及本地标记文件（非商业游戏 ROM）。它证明新入口能接入真实核心生命周期，不证明 FC／GBA 等模拟器兼容性。导入页面可达性已经验证；本轮未完整执行不同 ROM 类型的扫描入库验收。

## 证据

本地验证产物不属于提交内容，独立克隆仓库后需重新执行才能得到。

- [独立构建日志](../../artifacts/lead-acceptance/build/msbuild.utf8.log)
- [菜单 harness 日志](../../artifacts/lead-acceptance/harness.utf8.log)
- [消息表检查结果](../../artifacts/lead-acceptance/message-checks.txt)
- [中文及回退语言记录](../../artifacts/lead-acceptance/menu-results-v2.json)：其中英文那次测试因状态响应读取方式超时，不作为关闭流程通过证据。
- [最终关闭与缺失内容结果](../../artifacts/lead-acceptance/core-flow-result.json)
- [最终英文菜单操作记录](../../artifacts/lead-acceptance/en-populated/menu-transcript-v3.txt)
- [其他菜单驱动结果](../../artifacts/lead-acceptance/driver-results.json)
- [核心选择及缺失核心记录](../../artifacts/lead-acceptance/core-selection-results.json)

早期验收脚本曾遇到 RGUI 字体缺失、德文断言大小写和 CONTENTLESS 响应不带换行的问题；均先读取源码／日志确认原因，再修正隔离的验收配置或读取方式，没有为通过测试而修改产品逻辑。关闭按钮第一次点击仅确认提示，最终验收执行第二次确认并检查 CONTENTLESS，不把确认页误作已关闭。

## 复现命令

在 GameGate 根目录使用 PowerShell：

```powershell
$python = 'C:\Users\lenovo\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
$msbuild = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\MSBuild\Current\Bin\MSBuild.exe'
$root = (Get-Location).Path
& $msbuild pkg/msvc/msvc-2022/RetroArch-msvc2022.vcxproj `
  /p:Configuration=Release /p:Platform=x64 `
  "/p:OutDir=$root\artifacts\lead-acceptance\build\bin\" `
  "/p:IntDir=$root\artifacts\lead-acceptance\build\obj\" `
  -nodeReuse:false /v:minimal
& $python -B samples/menu/gamegate_library/build.py --msbuild $msbuild --run
& $python -B tools/gen_lbl_str.py
& $python -B tools/intl_conversion_check.py
& $python -B tools/sublabel_map_check.py --selftest
```

菜单交互使用原生 stdin 命令接口（独立测试配置开启 stdin_cmd_enable，关闭 network_cmd_enable）。MENU show/a/b 读取并操作实际界面。测试配置关闭二次退出和失焦暂停，但保留关闭内容确认。程序仅写隔离的测试数据目录。

## 尚未覆盖

- Linux／MinGW 构建、完整 Linux CI、无线程构建：当前环境缺少对应工具链，未安装依赖。
- Android、iOS、鸿蒙、macOS 实机。
- 实体手柄与触摸、长时间稳定性、游戏音频质量、BIOS、多种 ROM 的导入与核心兼容。
- 完整发行包、资源许可审查、自动更新和远端发布。

正式审查结果及调用记录单独写入开发进度记录，不将实现者自测报告当作独立审查结论。
