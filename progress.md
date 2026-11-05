# GameGate 开发进度

## 2026-11-04 隔离续作 GG-002

- 用户明确同意独立目录继续，create_worktree已建立 .agents/worktrees/feat/static-sources-isolated，分支feat/static-sources-isolated，基线73100fda362b8622981daa2163cff1bc2d6a39cb。
- 旧写任务d662e539-9b3e-47fd-84e4-7b048e8ba820原目录写权不变，不声称已停止；新任务绝对禁止原目录写入。重新整合须另查原工作区状态。
- 复制已批准GG-002设计及实施计划到新checkout，初始业务diff为空；现只有主Agent规划/文档修改。
- GG-002A1：functions.subagent_codex实现模型/URL/语言回退及测试；实际调用ID e4594b10-94c1-427f-bf13-5e0f3151ad91，已启动待交接。范围仅本checkout gamegate/catalog.c/h和samples/gamegate/catalog/；禁止提交和切分支、写原路径、安装依赖、修改文档。
- 先小任务交接并验收/审查，再A2解析器；B和C仍须最终完成。全链修复0/2。
- A1任务e4594b10-94c1-427f-bf13-5e0f3151ad91已停止写入并交接4个新文件，模型代码真实落盘。build.py运行到cl.exe查找时报WinError2，尚未编译测试。
- docs/sources/static-json-v1.md由主Agent在已约定文档写域编写，其出现属于预期并行工作；新任务不应将该变化误判为其他写者越界。
- 主Agent确认MSVC编译器实际存在：C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe。修复包只处理构建器环境/可执行路径与必要编译诊断，禁止扩产品范围，计入GG-002修复1/2。
- 修复者functions.subagent_codex，ID631cb325-631a-4d00-b2cf-a9881f4588f5，已停止写入并报告测试通过。主Agent随后实际独立运行Python -B samples/gamegate/catalog/build.py，退出0，输出catalog model tests passed，证据artifacts/static-sources/lead/a1-tests.utf8.log。源码、模型测试未被修复包改写，只有build.py更新环境查找。
- GG-002A1-REVIEW：functions.subagent_claude_code，ID cc1b1547-62b4-448a-a657-3349d633b9ae，已完成APPROVE_WITH_NITS，无P0/P1/P2；主Agent核对7文件哈希及untracked路径不变。
- A1 P3裁决：UI语言到BCP47映射在C包实施，不改变本包精确语言匹配契约；%XX路径不解码或转换本地路径，字面控制字节已拒绝，编码控制字符由后续请求构造保持原样，不把保留编码错误视为已验证注入；构建器仅本机BuildTools x64已验收，其余平台工具链适配延后。未把P3注册为本轮修复。
- GG-002A2：Codex d4b3384a-29f1-4901-b37f-f520dd3c585e 已实现并停止写入，新增catalog_parse.c/parser_test.c，扩展模型及build.py。主Agent已完整读取parser与测试并独立编译执行，exit0，model passed、parser491cases passed；日志artifacts/static-sources/lead/a2-tests.utf8.log。编译辅助进程保持输出句柄，shell已明确输出LEAD_A2_EXIT=0后关闭wrapper，不把wrapper取消状态当作测试exit0。全链修复仍1/2。
- GG-002A2-REVIEW：functions.subagent_claude_code，IDa0ae9d4b-8ff0-423d-b774-7b5d2a96f515，已完成APPROVE_WITH_NITS无P0/P1/P2；主Agent核对9个冻结文件hash一致。
- A2 P3裁决：region/version128字节是主Agent实现包约束，设计文本未写清，作为协议文档待明确项保留；空串和NULL展示时均按无资料处理，不立即改模型；解析创建/销毁/错误free是总量有界但非分片，B集成不得在菜单帧同步批量执行。当前不选择对A2代码追加修复，全链仍1/2。
- GG-002B1：开始独立HTTPS传输后端（Windows异步WinHTTP、Android纯Java工作对象及JNI轮询桥接）。源缓存/注册和菜单后续单独接线，避免大任务。允许本checkout新增gamegate/source_http*.c/h、Android共享Java helper及Activity小入口、samples/gamegate/source_http/。禁止改共享HTTP/TLS、parser、用户配置或原目录。真实Codex调用 e527a31e-d8b8-4661-bf88-76cfeb88d356 已完成并停止写入；主Agent实际独立重跑Windows与Java构建/测试均exit0，日志artifacts/static-sources/lead/b1-windows.utf8.log及b1-java.utf8.log；Android NDK/SDK编译未验收。
- B1正式Claude审查 d9353949-627f-49e8-8b99-26651eaf6148 REQUEST_CHANGES，4项P2：WinHTTP异步参数、Java Inflater所有权、Java尾段dot路径、离线测试依赖本机fixture。主Agent核对17个受审文件SHA256不变。
- FIX-2（全链2/2）选择修复全部4项P2，并选P3取消后owner清理、start/poll所有权契约、限定合法重定向状态；主Agent静态发现C resolver在无path基址补斜杠时需重新检查合并长度，要求修复边界并加回归。未证实可利用性，不宣称动态越界复现成功（先前隔离调用只输出VALIDATION_RESULT False）。
- P3 JNI极端VM关闭诊断、Win7兼容和其他工具链路径暂留限制；Windows deflate失败仅对应所测服务编码，不能将raw/zlib不同封装混为统一不支持。主Agent不会修改业务代码，由Codex修复并独立再验收/Claude复审。
- 用户截图曾出现模型提供方cyber_policy拒绝；未将其算作项目编译失败或某子任务完成证据，未重新触发被拒诊断。正式Claude报告已实际返回。
- FIX-2作者0971f5e6-a5de-4947-af0a-bfd190aaf8ae已停止写入，主Agent读完fix2.patch核对生产/测试变化并重跑：--offline exit0（owner37，本地TLS72/offline68，stub3）；live exit0（118通过0失败2跳过）；Java exit0（136通过无跳过）。独立日志artifacts/static-sources/lead/b1-fix2-*.utf8.log。
- B1复审 functions.subagent_claude_code ID e26d4015-c81b-4a51-9db8-87525dd279d7 已完成APPROVE_WITH_NITS，无P0/P1/P2，选定P2/P3修复已关闭；主Agent核对19文件哈希一致。剩余owner harness非Windows链接/测试常量及环境未验证为延期非阻断项。修复计数2/2，未重置。
- GG-002B2a：Codex283dbc69-9943-4364-9913-35745f263855完成且停止写入。主Agent独立MSVC真实VFS测试exit0、2034断言通过。Claude252f84db-9a69-4d29-834e-e649c20f081d APPROVE_WITH_NITS，无P0/P1/P2；12文件哈希核对通过。保留OOM错误分类及特殊VFS errno语义为P3限制，不选新修复。
- GG-002B2b：拟由Codex实现source_service.c/h、独立串行worker与UI快照、HTTP完成数据所有权移交、取消代次/关闭；允许必要的source_http*.c/h新增take_data接口及对应测试，禁止改store/parser/菜单/文档。输出samples/gamegate/source_service/真实回归；只在本checkout。调用ID fbc1d0e9-1e0e-407d-992a-8573c1d19095已完成并停止写入；主Agent读取完整service与HTTP扩展、线程测试，独立运行build.py --network退出0：11无线程断言、402确定性断言、真实HTTPS添加/保存/重开/浏览PASS（网络套件714断言）。证据artifacts/static-sources/lead/b2b-service.utf8.log；全链修复仍2/2。
- B2b-REVIEW：Claude cee1c432-a523-403c-a927-2cb7663a8b29 APPROVE_WITH_NITS，无P0/P1；主Agent核对23文件哈希一致。P2初始化cancel使实例NOT_READY属真实API限制，决定延期，不标关闭：当前尚无生产调用方，C包只在FETCH/APPLY/LOADING提供普通取消，INITIALIZING离开页面不cancel，退出直接free；通过新菜单有效调用约束避免触发。保留P3提交后load失败、唯一catalog_id、FETCH缓存复用、Android DONE一次JNI复制等边界。
- GG-002C开始：Codex独占原生菜单/控制器、MAIN_THREAD poll任务、i18n/label生成、Makefile/Griffin及必要Unity编译适配和真实菜单测试；禁止改变service/store业务契约或重置修复计数。所有源页面按localID/gameID查找，反复检查kiosk；不实现游戏下载/核心安装。完整Windows构建与MENU交互验收必须做，Android未具NDK不宣称通过。调用ID b27c6404-e776-40f7-befe-826acc92ffb4未完成交接；2026-11-05读取Win32_OperatingSystem确认主机09:00:14+08:00重启，旧本地执行进程已终止，可在保留改动后继续该未完成首次实现包。


- B2b预研：task_queue.h:176提供RETRO_TASK_FLG_MAIN_THREAD，可保证前端线程驱动request owner，即使全局threaded tasks开启也不换到worker；store/parse仍需专门后台工作单元，避免大对象create/error/destroy阻塞菜单。





- 独立只读WinHTTP研究：functions.subagent ID e25c2537-8d84-4ee5-b9d2-cadcb3699371，已完成报告。原生异步回调无固定线程且可能重入，不能callback中递归ReadData；owner分帧推进，callback仅发布事件；HANDLE_CLOSING是释放context的屏障。运行时threaded tasks关闭与编译HAVE_THREADS=0要分别处理，后者不可直接使用伪原子变量跨线程。
- 用户明确拒绝Windows-only后端交付，随后选择本轮Windows和Android，同时要求记录其他平台后续补齐。已更新设计平台段：Linux/macOS/iOS/iPadOS/原生鸿蒙在线传输后续适配，不算本轮已支持。
- 环境核查：ANDROID_HOME/SDK_ROOT/NDK_HOME均未设置，常见LocalAppData Android/Sdk不存在，PATH无adb/gradle；发现JDK21 C:/Program Files/Java/jdk-21.0.4+7/bin/javac.exe。可用纯Java编译/网络测试，不等于Android APK/实机验证。
- Android现代phoenix工程compileSdk36、NDK29.0.14206865、minSdk21，Java sourceSets含phoenix-common/src；Java请求helper可独立于Activity编写，再通过现有JNI桥接。仍未实现B，不声称已支持。







## 2026-11-04 GG-002 早期归档 启动

> 以下为隔离续作之前的历史记录，保留当时状态；不代表当前仍在等待或阻塞。原始文档提交：`45f62a5c63`。

- 用户确认静态JSON游戏源设计并授权继续实现。
- 基线73100fda362b8622981daa2163cff1bc2d6a39cb，tracked/staged为空；已有未跟踪设计文档由主Agent创建，保留。
- 任务包GG-002A，functions.subagent_codex实现目录协议/模型及测试；仅gamegate/catalog.c/h和samples/gamegate/catalog/写域，不改菜单/运行核心/libretro-common/文档。调用ID d662e539-9b3e-47fd-84e4-7b048e8ba820，已启动待交接。
- GG-002B预研：functions.subagent，ID08243388-22aa-4ff5-898f-41e6754a086b，只读检查HTTPS限额/重定向/TLS/任务生命周期，不写任何代码；与A实现无写域重叠。
- 普通只读研究可并行检查HTTPS有界接收/缓存/生命周期；不得写代码或替代Codex与Claude。
- 主Agent完成实际独立测试后交functions.subagent_claude_code只读审查，每次审查前后校验内容和SHA256。
- 验证以MSVC真实C程序和相关生成/菜单检查为准；完整UI与HTTPS生命周期在后两包，不能以parser通过冒充GG-002完成。
- 全链修复计数0/2。未授权推送，不修改认证或安装依赖。
- 自动续跑第1轮检查：当前业务源码尚无写入，A实现子任务未发完成通知；不重复派发写任务，不据进程列表猜测任务已终止。
- 已独立核查net_http.c:1746和net_socket_ssl_retro.c:2355：retro TLS连接忽略nonblock参数并同步完成握手；net_http.h:70规定全局缓存/连接池单线程驱动。这是B包接入设计约束，不是已修复问题。
- 实施计划已落盘docs/superpowers/plans/2026-11-04-static-game-sources-implementation.md。重新检查MSVC可用，PATH无gcc/clang/make/docker；测试环境限制没有被忽略。
- 第2轮：使用FileSystemWatcher有界等待45秒，无catalog文件创建事件；未忙轮询。额外只读Codex探测fc4ffe30-c746-49c2-aabe-435e675ed449成功执行git rev-parse HEAD并完成，证明后端可达，但不证明原实现者已停止。原A任务仍无完成/取消确认，不重新分配其写域。
- 独立研究确认vfs_implementation.c:2193通过MoveFileExW(MOVEFILE_REPLACE_EXISTING)支持Windows目的文件替换，B包可复用文件流rename能力，不能因此承诺跨所有VFS提供者的断电事务原子性。

## 2026-11-02 第一批任务启动

- 用户要求开始任务。
- 已加载 codex-claude-review 与 planning-with-files-zh；继续遵循此前 brainstorming 的设计确认门槛。
- Git 检查通过：起始 HEAD 9fa533cecd2ffddcfc4fa29fe07fea320577442f，staged／unstaged／untracked 均为空。
- 已读取编码规范前 180 行，其余继续读取。
- 真实工具中没有 subagent_codex、subagent_claude_code；没有相应调用 ID。已向用户说明后续降级及影响。
- 尚未修改业务源码、运行回归、创建功能分支、提交或推送。

## 2026-11-02 工具恢复检查

- 当前注册 functions.subagent_codex 与 functions.subagent_claude_code，均已实际调用。
- Codex：53962b25-7532-4724-8f1c-07960e484159；GG-001-PROBE-CODEX；只读接入分析，已启动待结果。
- Claude Code：515ba9c1-e449-457f-a936-22ce7f5a984a；GG-001-PROBE-CLAUDE；只读方案风险分析，已启动待结果。
- 调用启动不等于执行完成，等待实际结果。
- 11 个源码和规则文件 SHA-256 保存在 artifacts/agent-probes/baseline.json；探测期间冻结源码，主 Agent 仅维护规划文档。
- UI 选择问题此前被中止，尚未获得用户选择。
- 更新本文件时出现一次文件版本过期提示；已重读，原文与读取基线一致，再按当前版本更新。

## GG-001-IMPL 调度记录

- 两项探测均成功完成：Codex 与 Claude Code 实际可用；不再降级。两者都仅只读，主 Agent 在结束后确认 11 个源码文件哈希不变、tracked diff 为空。
- 用户通过选择题明确批准原生菜单首批方案，设计保存于 docs/superpowers/specs/2026-11-02-native-library-design.md。
- 主 Agent 创建 feat/native-game-library，HEAD 仍为 9fa533cecd2ffddcfc4fa29fe07fea320577442f。
- 实现者：functions.subagent_codex，任务 ID 45eaf5b8-ef29-4cac-8f76-84b10cc0aaf9；已完成并停止写入，无 commit/push。
- 主 Agent 已独立构建（退出0）、重跑真实菜单 harness（退出0）、检查消息生成幂等／转换／packed与format checker（均0）。
- 实际菜单验证：英文、补齐字体后的简中、德文英文回退、kiosk、四套菜单驱动入口与导入导航通过。2048 从集合实际加载，二次确认关闭后验证 CONTENTLESS，回条目页再后退回原集合。缺失内容和核心不启动；无核心时进入原生核心选择。
- 验收脚本早期失败原因：中文 RGUI 缺少原生字体、德文断言大小写、CONTENTLESS 响应不带换行；读取源码确认后修正隔离配置／协议读取，不改业务代码。最终证据在 artifacts/lead-acceptance。
- GG-001-REVIEW-0：functions.subagent_claude_code，ID ea41ffc1-c5c6-4c77-aba7-a2f085f9d27b；已完成，APPROVE_WITH_NITS，无P0/P1/P2。主Agent结束后核对19个文件SHA256、完整diff、untracked路径全部一致。审查者报告曾在仓库外创建并删除临时diff比对文件，仓库内容未变；不将其描述为全文件系统零写入。
- GG-001-FIX-1 裁决：选择修复P3-1（固定Windows SDK）、P3-3（原Playlists及kiosk非空集合测试）、P3-4（中文说明精确值及回退）、P3-5（本地链接标注）、P3-6（表项对齐）。均交Codex，不由主Agent代修。
- P3-2延期：Linux/MinGW路径未实跑且make配方解析受假设限制；当前没有工具链，不能靠未经验证的重写宣称跨平台通过。CI接线及异步dirwalk专项覆盖也保留为后续工作，不属于已完成验证。
- 修复计数进入1/2。Codex原实现者及Claude审查者均已结束，写权限已交给 functions.subagent_codex，任务 ID 8d9c0e87-ddea-4504-acb1-483abde5b496；交接基线为round0快照。仅允许修复指定的测试脚本、测试源码、交付说明和一处表项空格。
- 未验证：Linux/MinGW、其他设备实机、实际扫描落盘、实体手柄、声音质量与完整视觉资产。详见 docs/development/native-library-delivery.md。
- 实现范围：menu 原生入口及必要 callbacks／枚举，msg_hash 消息注册与 GameGate 自有翻译源，必要生成文件和聚焦测试。复用 playlist 与导入，不新建总库文件，不改变运行循环／全局退出设置。
- 主 Agent 写域：task_plan.md、findings.md、progress.md 以及 docs 下设计／交付文档；实现者禁止修改这些文件。
- 禁止：上游参考目录、生产配置、远端、任意依赖安装、全仓改名、核心运行引擎重构；实现者不得 commit/push。
- 验证：MSVC Release x64 构建（产物 artifacts/native-library），消息生成／格式检查、原生菜单与集合导航测试；缺乏 Linux／MinGW 环境如实记录。主 Agent 将独立执行。
- 当前修复计数1/2。FIX-1任务8d9c0e87-ddea-4504-acb1-483abde5b496已完成并停止写入；主Agent核对仅约定四文件变化，独立重跑扩展harness退出0，PASS 0 failures，日志artifacts/lead-acceptance/fix1-harness.utf8.log。
- GG-001-REVIEW-1：functions.subagent_claude_code，任务ID ddf4d193-da33-4040-80cf-ebe081c31caf，已完成，APPROVE_WITH_NITS。选定P3-1/3/4/5/6全部解决，无新增P0/P1/P2。主Agent最终核对round1的19文件SHA-256、完整diff、暂存diff和未跟踪路径全部一致。
- 保留非阻断项：P3-2 Make解析与Linux/MinGW未验收；新harness未接samples/CI；测试OK失败时栈边界诊断可增强。后两项分别属于流水线接入和失败诊断质量，不影响当前产品行为，延期处理，未用额外修复轮次扩范围。
- round1/fix1.diff的整文件噪声是快照文本换行转换伪影；原始文件哈希和Git diff证实菜单回调文件仅多一空格，不将工件噪声计为源码重写。
- 最终修复轮数1/2，测试0 failures；主Agent只更新审查范围外行政日志，未再修改被审源码、测试、设计和交付说明。未commit/push；实现与两轮正式审查全部完成。
- 目标已在合法自动续跑回合标记 complete（revision 3），不再需要重复收尾。用户随后明确要求“提交一版”；主 Agent 核对19个受审文件与round1快照一致、源码diff未变、暂存区为空，仅更新行政日志后按明确文件路径创建本地提交，不推送。提交标识以 Git 日志为准。
- 另一次窗口截图尝试因无法确认目标窗口处于前台而主动中止，未抓取桌面；不声称完成视觉截图验收。
- 主 Agent 已在 artifacts/lead-acceptance 准备英文有集合、中文空库、德文回退、kiosk 四组独立配置及测试数据，不写实现者源码或产物目录。
- stdin MENU 接口试验可读取真实菜单和导航；独立配置关闭二次退出后 QUIT 正常返回0。此为验收通道准备，不是新功能验收。
- 子代理使用完成通知接收结果；job_output 不管理 subagent UUID，曾返回 unknown job，后续不以此查询子代理。

## 2026-11-05 C包中断恢复

- 当前自动目标第4/18轮，保持active。源代码最近写入2026-11-04 20:55；最后RGUI自测在中文标题断言处失败，未收到实现完成报告。
- 已保存56个修改/新增文件的内容与SHA256、tracked/staged diff及untracked列表至artifacts/static-sources/c-resume-baseline，保留所有现有实现。主机重启是旧本地进程终止证据，不把无job列表或文件静止当完成证据。
- 主Agent真实对照复现同一已有exe、language=12：无资源时English Game Sources=true/中文false；提供4个已有RGUI位图字体后中文true/English false。使用独立lead/c-language-check配置与复制资产，未改生产代码或父目录。
- samples/menu/gamegate_sources/build.py使用menu_config_directory，configuration.c:2250实际键为rgui_config_directory；测试fixture未生效导致源缓存落到测试exe的bin/config。接续须修正夹具隔离，不覆盖/清理原工作区或旧证据。
- 本次为重启中断后的C首次实现接续，不把它伪报为已验收包；全链既有修复计数仍2/2，不重置，A/B代码及延期API限制保护不变。

- C首次实现接续真实调用functions.subagent_codex，ID9888faf4-0f21-46e0-86d1-a50954a214f9，已启动；写域沿用原C，不改A/B行为。要求最终源码重建、字体有效的中文/RGUI/Ozone及原库回归，再交接。

## 2026-11-05 C独立验收及最终审查

- C接续9888faf4-0f21-46e0-86d1-a50954a214f9正式停止写入并交接，完整diff已读；接续只改4个C范围内源码/测试文件，其余已有改动保留。
- 主Agent全新lead/frontend/bin与obj完整Release x64构建，实际MSBuild exit0；随后以该exe独立跑RGUI/Ozone/XMB/glui，每驱动7进程共28次完整MENU+生命周期路径，所有进程退出0。证据review-final/lead-acceptance.json、lead/menu-acceptance各driver日志/菜单逐条记录。
- 主Agent重跑旧游戏库harness（实现者编译的harness exe）0 failures；intl_conversion、sublabel selftest、packed/format selftest与全表均0；gen_lbl_str内容SHA256不变，diff check通过。未重做核心加载专项，未验证AndroidNDK/实机、Linux/MinGW。
- ci_local selftest常规调用确实失败：WSL无发行版导致fixture首echo exit1；GitBash明确路径诊断此前60s超时，原CI代码未修改，保留失败限制。
- 最终实际Claude工具调用01a2b2fb-0ff5-469b-a499-8c7e5993be02进行中，56个业务/测试/设计/交付文档文件冻结于artifacts/static-sources/review-final。docs/development/static-sources-delivery.md与源参考已整合并纳入审查。审查期间只更新行政文档。无提交/推送，目标尚未标complete。

- CI自测补充：job win-bash-c6e5be4b181d440b已完成。使用内存wrapper仅将subprocess参数bash换为C:/Program Files/Git/bin/bash.exe，并把该子进程env键统一大写；未改ci_local.py/工作流/系统PATH。真实cmd_selftest完整执行输出101 workflows, ok及CI_EXPLICIT_EXIT 0。此前普通调用WSL失败和60s诊断超时仍保留；本次不是跳过断言/fixture，也不代表执行所有CI job。PyYAML缺失比较仍跳过。最终审查窗口不改已冻结交付文档，待报告后同步这条新证据。

## 最终裁决与文档复核

- 最终集成Claude01a2b2fb-0ff5-469b-a499-8c7e5993be02返回APPROVE_WITH_NITS，无P0/P1，不阻断Windows试用；主Agent核对56文件SHA256一致。
- 主Agent接受最近错误未持久化为本批P2实现偏差，保留设计原要求和后续补齐事项，不称用户已豁免。初始化cancel API P2也保留，菜单规避已验证不等于API修复。P3文本行选择跳动/全局错误归因/unsupported提示/输入启动失败及NOMEM反馈/注释归属列明延期；不启第三轮代码修复。
- Windows便携预览只复用已验收exe及既有本地assets/licenses/config，追加有效rgui_config_directory，无新业务代码。复制到独立路径启动中文Ozone空态、正常QUIT exit0；zip11883844bytes，SHA256 4becdf576d8775bd275cf693b3174b0caaf623581bbd4ada138a7f2537bdea58，152文件，无用户或测试registry，无ROM/BIOS/core；包属于本地预览非正式发布。
- 三份文档同步最终结论/CI新增证据/预览入口/偏差；其余代码及受审文件hash不变。新快照review-final-docs，Claude只读文档复核db28b4a6-7199-4268-b8a3-60521e82c21b进行中。无提交推送，旧原工作区未覆盖。

## GG-002本轮收尾

- 最终文档Claude复核db28b4a6-7199-4268-b8a3-60521e82c21b APPROVE，无阻断；主Agent再次核对3份最终文档及其余53份代码/测试/上下文hash一致，暂存区为空。
- 本轮功能开发及Windows试用验收收尾；Windows完整交互、分层真实测试、最终代码与文档审查闭环完成。Android仅Java桌面验证和JNI静态接线，原生构建/设备验收未完成；其他平台在线能力后续适配。
- 保留两项P2：最近错误未持久化的实现偏差、初始化cancel API限制（菜单已避免调用）；P3与测试缺口见最终交付说明。不称零缺陷或全平台通过。
- 预览包已提供，源码仅隔离工作树，未提交/推送/合入原工作树；无测试应用遗留。所有实际工具ID及验收证据见上述记录。
