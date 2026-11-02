# GameGate 开发发现

## 2026-11-02 第一批启动

- 起始工作区干净，HEAD 为 9fa533cecd2ffddcfc4fa29fe07fea320577442f。
- 现有 tasks/task_content.c 的 task_push_load_content_with_new_core_from_menu 接收核心路径与内容路径；HAVE_DYNAMIC 分支执行进程内加载并处理菜单失败返回。
- 必须按现有菜单／配置／消息体系接入，不能只改一个显示文件。
- 上游规则要求 C89、保持架构、不新增锁、遵守栈预算和不阻塞主线程。
- 已有 Windows 原版构建记录，不重复开展全面上游验证。
- 用户尚未确认具体新首页布局，先整理可审阅的接入设计。
- 已读完整 CODING-GUIDELINES（336 行）。menu 子树未发现更细 AGENTS.md。
- menu/menu_displaylist.c:17744 构建主菜单；同文件已有收藏与加载内容入口。
- menu/cbs/menu_cbs_ok.c:2986 的 action_ok_playlist_entry_collection 处理列表选择，调用现有 task_content 入口。
- 第一批可复用播放列表存储、菜单和内容加载，不需要为本地原型另造游戏库数据库或立即引入跨平台 UI 框架；元数据模型仍应后续独立设计，不能把列表标题当成作品唯一 ID。

## 工具恢复与设计确认

- 用户明确选择“按推荐方案实现”；设计见 docs/superpowers/specs/2026-11-02-native-library-design.md。
- Claude Code 探测 515ba9c1-e449-457f-a936-22ce7f5a984a 已完成，源码风险报告可用；不是正式代码审查。
- 主 Agent 核对 11 个源码／规则文件哈希不变，tracked diff 为空。
- 原有关闭内容返回列表依赖导航标签，新入口需保留条目导航身份。
- 中文表及 label blob 是生成资源，新增翻译必须可重现。
- 已执行 tools/ci_local.py which 检查拟改菜单／消息文件的关联任务，后续按实际 diff 更新测试范围。
- 本机 PATH 未发现 docker/gcc/clang/make；WSL 列表没有发行版。MSVC 已可用，不能声称 Linux／MinGW 回归通过。

## 独立验收准备

- 已用既有 baseline.exe 验证 stdin_cmd_enable 的 MENU show/select/a/b 接口，可以读取实际菜单和导航路径，不需模拟全局键盘。
- 首次就绪后的菜单条目会刷新（历史／收藏初始化），测试必须按当前标签重新找 index，不能硬编码启动时下标。
- 首次协议探测清理时超时终止；独立配置补充 quit_press_twice=false 和 pause_nonactive=false 后，QUIT 自然退出码为0。该设置只作用于 artifacts/lead-acceptance 配置，不修改产品默认行为。
- 主 Agent 产物写域 artifacts/lead-acceptance，与 Codex 的 artifacts/native-library 分离；这些探测只是验收工具准备，尚未证明新功能通过。
