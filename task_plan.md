# GG-002 隔离工作区实施计划

## 当前状态

用户已确认静态JSON游戏源设计，并在旧外部实现任务状态不可确认后，明确同意使用独立新目录继续。本目录：D:/Project/XJ/Game-Gate/Code_Source/GameGate/.agents/worktrees/feat/static-sources-isolated，分支feat/static-sources-isolated，基线73100fda362b8622981daa2163cff1bc2d6a39cb。

## 隔离边界

- 旧任务d662e539-9b3e-47fd-84e4-7b048e8ba820状态仍未知，原目录及其gamegate/catalog.c/h写域不转交，也不覆盖。
- 新任务只写本隔离checkout；不得访问原目录进行实现或测试输出。共享Git对象不等于共享工作树，禁止子任务commit/checkout/merge等修改引用。
- 最终整合前再次读取原目录状态，保留可能出现的旧任务产出并明确择取，不直接复制覆盖。

## 执行顺序

1. [完成] GG-002A1：模型／URL／语言回退，真实MSVC C测试通过，Claude APPROVE_WITH_NITS无P0/P1/P2，快照核对通过。
2. [完成] GG-002A2：严格JSON解析和491个用例，主Agent独立测试exit0，Claude APPROVE_WITH_NITS无P0/P1/P2，9文件快照核对通过。
3. [完成] GG-002B1经FIX-2修复后，主Agent独立offline/live/Java均exit0，Claude e26d4015-c81b-4a51-9db8-87525dd279d7 APPROVE_WITH_NITS，无P0/P1/P2。19文件最终哈希核对通过，保留明确平台/测试限制。
4. [完成] GG-002B2a独立真实VFS测试2034断言通过；Claude252f84db-9a69-4d29-834e-e649c20f081d APPROVE_WITH_NITS无P0/P1/P2，12文件哈希复核通过。
5. [完成] B2b实现/独立--network验收/Claude审查闭环；保留初始化普通cancel需free重建的P2限制，由C有效调用边界避免触发，不标API已修。
6. [完成实现与主验收] C接续Codex9888faf4-0f21-46e0-86d1-a50954a214f9已停止写入。主Agent新OutDir/IntDir完整Release x64构建exit0；独立exe四驱动各7次菜单流程共28进程退出0；旧库harness及消息检查通过。环境CI selftest仍失败，不算全CI通过。
7. [完成] 最终代码审查01a2b2fb-0ff5-469b-a499-8c7e5993be02 APPROVE_WITH_NITS，无P0/P1；最终文档复核db28b4a6-7199-4268-b8a3-60521e82c21b APPROVE。最终hash复核一致，Windows试用包已提供；两项P2与平台验证边界明确保留。未提交/推送。

设计：docs/superpowers/specs/2026-11-04-static-game-sources-design.md。模块拆小不改变已批准功能，也不代表仅完成A1就交付GG-002。

## 双模型与写域

functions.subagent_codex实际实现/修复，主Agent独立验收，functions.subagent_claude_code只读审查。当前A1允许本checkout gamegate/catalog.c/h及samples/gamegate/catalog/，禁止其他业务文件或主Agent文档。主Agent只写规划/设计/交付文档和独立验收产物。

修复计数GG-002为2/2（A1构建器、B1审查修复）；隔离不重置计数。若本轮之后仍有成立的阻断问题或验收失败，需停止报告，不擅自无限修复。后续尚未实施的新功能仍按原目标串行推进，不假报已完成。

## 本轮平台决策

用户明确要求Windows和Android均实现在线游戏源（不是Windows-only）；Linux、macOS、iOS/iPadOS、原生鸿蒙在线获取列为后续适配事项。通用协议/缓存/UI共用；传输可分别使用系统异步接口。本机JDK21位于C:/Program Files/Java/jdk-21.0.4+7，纯Java网络逻辑可实测；未发现Android SDK/NDK，不声称APK或实机已验收。


## 工具与验证

Python：C:/Users/lenovo/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe。
VS2022 BuildTools：C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools，MSVC v143可用。
产物仅本checkout artifacts/static-sources/，主验收使用lead/子目录。真实模型/URL/fallback测试，不以源码文本搜索替代执行。Linux/MinGW未发现工具链，不能假装通过。

主Agent保存审查前后源码内容、哈希、diff、未跟踪列表。保持C89与上游线程/栈预算规则。旧本地游戏库和便携资产不修改。

## 2026-11-05 文档归档合并

原工作区5份启动阶段文档已原样提交为 `45f62a5c63`，并并入 develop。早期研究与进度作为历史保留在 findings.md / progress.md；本文件保留后续已完成的验收状态，避免恢复为早期“等待/阻塞”。原始启动计划可从该提交中的 task_plan.md 查阅。已完成代码提交为 `85ea029c75`，已合入 develop；未推送远端。
