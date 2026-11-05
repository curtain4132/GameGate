# GG-002 静态游戏源：本地开发交付记录

状态：实现、Windows 独立菜单验收与最终代码集成审查已完成，结论为 APPROVE_WITH_NITS，无 P0/P1；保留下述延期项。尚未提交、推送或发布正式安装包。源码基线 `73100fda362b8622981daa2163cff1bc2d6a39cb`；工作分支 `feat/static-sources-isolated`，保留在独立工作树。

## 使用范围

原生主菜单新增“游戏源 / Game Sources”。用户可输入 HTTPS 目录地址，浏览游戏资料，刷新、停用、重新启用或移除源。移除先确认，仅作用于该源登记及缓存，不删除本地游戏或存档。已有“我的游戏库”保留。

本批只浏览资料：不下载游戏、不安装核心、不启动目录中的游戏，也不下载封面。源提供的外部资料 ID 不会触发元数据库查询。目录编写规则见[JSON v1 参考](../sources/static-json-v1.md)。

本轮目标平台为 Windows 和 Android。Windows 已完成原生程序构建及菜单交互验证。Android 已实现 Java 请求与 JNI 接口，Java 网络逻辑在桌面 JDK 21 验证通过；本机缺少 Android SDK/NDK，未验证 Android native 构建、APK 或设备运行。Linux、macOS、iOS/iPadOS、原生鸿蒙在线后端留待后续适配，不能宣称本批覆盖六端。

## Windows 试用步骤

建议使用[Windows x64 本地试用包](../../artifacts/static-sources/portable/GameGate-GG002-Windows-x64-preview.zip)：完整解压后运行 GameGate.exe，默认中文 Ozone，包含本地已有菜单资源及许可文件，无测试源登记、ROM、BIOS或核心。它是开发预览，不是正式发行版；二进制对应当前未提交工作树，不仅是基线提交。

试用包在复制后的新目录使用便携配置启动，中文“游戏源”及空态、QUIT退出0已通过；没有额外截图视觉结论。独立验收原始程序仍位于[Windows 测试程序](../../artifacts/static-sources/lead/frontend/bin/RetroArch-msvc2022.exe)。

1. 解压试用包并运行 GameGate.exe，在主菜单进入“游戏源”。
2. 选择“添加 HTTPS 游戏源”，输入符合 JSON v1 的目录地址。
3. 等待完成。校验成功的源才会出现在列表中；失败时页面显示原因。
4. 进入源详情，选择“浏览缓存游戏”，再进入游戏详情。
5. 在源详情可刷新、停用或重新启用。移除操作进入确认页面，“取消移除”返回原页。

展示模式（kiosk）只提供已启用源的缓存浏览，隐藏并拦截管理操作。来源文件应只包含用户有权使用的内容；本功能不提供官方 ROM 托管。

中文 RGUI 依赖四个原生位图字体；缺少字体时，上游会将界面整体回退英文。Ozone/XMB/MaterialUI 也需相应菜单资源。本轮使用隔离目录中的已有资源进行文本/导航验收，未完成所有页面的视觉和触摸验收。

## 数据与失败处理

- 数据目录为配置的菜单配置目录下 `gamegate/sources`。实际配置键为 `rgui_config_directory`；为空时使用当前配置文件目录或既有默认菜单目录。
- 注册表使用客户端生成的本地 ID，远端 source.id 不作文件路径。相同远端 ID、不同地址可并存；地址按完整字节精确去重，查询参数不排序、不删减。
- 每次有效刷新写独立版本缓存，完整写入和 flush/close 成功后，再替换注册表作为发布点。失败保留旧登记及有效缓存；不承诺突然断电时的目录 fsync 持久性。
- 停用、删除及其他修改推进代次；旧刷新不能恢复已移除源。初始化、下载、验证及存储有明确状态；繁忙时拒绝新操作并提示，不能把“请求已接受”当成完成。
- 网络由固定 owner 线程推进；解析、文件操作和大目录释放由专用 worker 执行，不依赖全局“线程任务”开关。界面按稳定源 ID / 游戏 ID 读取独立快照。
- HTTP 仅接受 HTTPS，保持默认 TLS 证书校验，最多跟随五次允许状态的重定向。解压后的目录最多 4 MiB，总传输截止时间为 30 秒。
- 最近一次源错误在菜单进程内记录，成功刷新可清除；当前不会把该错误写入注册表，重启后不保留错误历史。

## 独立验收证据

下列为主 Agent 实际运行，不能由实现者报告替代。产物均在本工作树的 artifacts 中，不提交。

| 检查 | 结果 |
|---|---|
| 协议模型、URL、多语言回退及 JSON 解析 | MSVC 构建及模型测试通过，解析用例 491 个通过 |
| Windows HTTPS 修复版 | 在线测试 118 通过、0 失败、2 项明确跳过；后续 take_data 接线的真实网络服务流程通过 |
| Java HTTPS | JDK21 `--release 8`，136 通过、无跳过；不代表 Android 实机 |
| 注册及有效缓存 | 真实 VFS、故障注入、重开、代次与损坏隔离，2034 项断言通过 |
| 异步服务 | 11 个无线程断言、402 个确定性断言；真实 HTTPS 添加、保存、重开、浏览通过（网络套件 714 个断言） |
| Windows 完整程序 | 全新独立 OutDir/IntDir，Release x64 MSBuild 构建及链接退出 0 |
| RGUI / Ozone / XMB / MaterialUI | 四驱动完整套件均通过，每驱动七次启动，正常 QUIT 退出 0 |
| 原“我的游戏库” | 重新运行真实菜单 harness，0 failures；未重做核心游戏运行专项 |
| 消息表 | label 生成后内容 hash 不变；intl conversion、sublabel selftest、packed/format selftest及全表检查退出 0 |
| CI 工具自测 | 本次检查明确选择已有 Git Bash并规范化子进程环境键后，101 workflows, ok，selftest退出0；不是完整CI矩阵执行 |

四驱动套件均涵盖英文/简中、德文缺译回退、逐字段资料语言回退、无源、非法 URL/JSON、添加、详情、刷新、启停、移除取消/确认、重启 kiosk 缓存。另有同远端 ID 双源隔离、128 字节 gameID 返回选择、HTTP 503 刷新后缓存 hash 保持、FETCH 取消和初始化返回/退出。

主要证据：

- [独立完整构建](../../artifacts/static-sources/lead/frontend/build.log)
- [RGUI](../../artifacts/static-sources/lead/menu-acceptance/rgui.log)、[Ozone](../../artifacts/static-sources/lead/menu-acceptance/ozone.log)、[XMB](../../artifacts/static-sources/lead/menu-acceptance/xmb.log)、[MaterialUI](../../artifacts/static-sources/lead/menu-acceptance/glui.log)：完整逐条 MENU 记录位于同目录各运行子目录。
- [协议测试](../../artifacts/static-sources/lead/a2-tests.utf8.log)、[存储测试](../../artifacts/static-sources/lead/b2a-store.utf8.log)、[服务测试](../../artifacts/static-sources/lead/b2b-service.utf8.log)
- [Windows 网络](../../artifacts/static-sources/lead/b1-fix2-live.utf8.log)、[Java 网络](../../artifacts/static-sources/lead/b1-fix2-java.utf8.log)

## 复现

在隔离工作树根目录，使用已安装的 Python、MSVC BuildTools 和 JDK21。菜单脚本要求 `--assets` 包含有效 RGUI 中日韩/英文位图字体及相应驱动资源；资源不会自动安装。

```powershell
$python = 'C:\Users\lenovo\.dsh\dsh-runtimes\dsh-primary-runtime\dependencies\python\python.exe'
& $python -B samples/gamegate/catalog/build.py
& $python -B samples/gamegate/source_http/build.py --offline
& $python -B samples/gamegate/source_http/build.py
& $python -B samples/gamegate/source_http/build_java.py
& $python -B samples/gamegate/source_store/build.py
& $python -B samples/gamegate/source_service/build.py --network
& $python -B samples/menu/gamegate_sources/build.py --build
& $python -B samples/menu/gamegate_sources/build.py --run --driver rgui --assets artifacts/static-sources/menu/assets
```

将最后一条的 driver 分别替换为 `ozone`、`xmb`、`glui` 可运行相同套件。网络用例使用虚构目录及公开测试服务，服务可用性会影响结果；失败不能通过放宽断言计为通过。所有测试配置、注册表、缓存、截图或可执行文件都应留在 artifacts。

## 未关闭限制

1. **初始化 cancel API（P2，延期）**：直接在 service 初始化时调用普通 cancel 会使实例需 free 后重建。生产菜单不提供此状态的普通取消；离开页面不取消服务，退出走 free。相关菜单路径已测试，但 API 本身未修复。
2. **Android 未构建/实机验证**：JNI 完成时仍有一次最多 4 MiB 的 Java→native 数据复制；take_data 仅避免之后交给存储 worker 的第二次复制。尚无 Android 帧时长证据。
3. **Windows 网络覆盖**：无 Content-Length 超限流、真实慢滴流截止时间仍为明确跳过项。deflate 测试只代表所用服务的编码封装；未增加通用 inflate fallback，旧 Windows/WinHTTP 未验证。
4. **特殊失败 UI**：尚未做 APPLY BUSY、提交后读缓存失败、刷新删除当前游戏的确定性 UI 故障注入；底层对应测试/静态检查不能替代这些界面场景。
5. **CI 环境**：普通 `tools/ci_local.py selftest` 仍会选到无发行版的 WSL Bash而失败；首次指定Git Bash诊断超时。随后仅在本次检查中选择已有Git Bash绝对路径、统一子进程环境键，完整selftest输出 `101 workflows, ok`、退出0；未改源码/工作流/系统PATH，也未跳过fixture。证据见[CI环境验证](../../artifacts/static-sources/lead/ci-check/explicit-result.json)。PyYAML不存在，解析器交叉比较仍跳过；这不等于执行所有CI job。Linux、MinGW、完整应用 `--disable-threads` 和 Android NDK均未验证。
6. **可移植性及错误语义**：owner cleanup harness 的非 Windows 链接适配待补齐；特殊 VFS 的 errno/原子 rename、解析 OOM 的错误分类仍有限制。源存储只适用于可信本地目录及单进程串行写者。孤儿版本文件不会被加载，当前不自动扫描回收。

7. **最近错误持久化（P2，交付偏差）**：已批准设计要求注册表保存最近错误，当前仅在运行期保存。主Agent接受为本次非阻断偏差，后续补齐；理由是当次失败可见、有效缓存不损坏，但跨重启诊断信息会丢失。该需求没有被标为已实现。
8. **UI 非阻断细节（P3）**：信息行刷新时的选中位置、全局错误在不同源详情中的归因、无服务能力时的提示及瞬时创建失败重试、输入框失败反馈、注释归属仍待改善。

实际实现由 `functions.subagent_codex` 完成，主Agent独立验收，Claude以 `functions.subagent_claude_code` 只读审查。最终代码集成审查调用 `01a2b2fb-0ff5-469b-a499-8c7e5993be02` 返回 APPROVE_WITH_NITS，56个冻结文件哈希一致。主Agent接受P2/P3为上述已披露延期项，不声称零缺陷或跨平台全验收；完整调用与裁决见项目 progress.md。代码修复轮次2/2，未重置。
