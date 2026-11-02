# GameGate 第一批开发计划

## 当前状态

GG-001 首批原生游戏库已完成实现、主 Agent 独立验收和 Claude 最终复审。结论 APPROVE_WITH_NITS，无 P0/P1/P2 阻断问题。本批按用户授权在 feat/native-game-library 创建本地提交，具体提交标识以 Git 日志为准；不合并或推送。

## 基线与范围

- 仓库：D:/Project/XJ/Game-Gate/Code_Source/GameGate。
- 起始 HEAD：9fa533cecd2ffddcfc4fa29fe07fea320577442f，起始 tracked diff 为空。
- 已确认：RetroArch 二开、GPL-3.0、仅 App 内运行、不使用 Rust。
- 用户明确选择原生菜单方案；本批是已有集合和原生导入的游戏库导航页，不是全新界面或新存储引擎。
- 保留上游参考目录，无新增上游通用全面验证，无远端操作。

## 阶段

1. [完成] 两种真实子代理调用探测与只读接入分析。
2. [完成] 用户确认首批方案，设计落盘。
3. [完成] Codex 实现，主 Agent 独立构建、菜单和核心生命周期验收。
4. [完成] Claude 首审，Codex 一轮指定小修，主 Agent 重跑扩充测试，Claude 最终复审。
5. [完成] 两轮审查前后内容／SHA-256／diff／未跟踪路径核对，交付记录与限制保存。

## 验证结论

- Windows Release x64 构建及真实前端菜单 harness 通过，最终 0 failures。
- 原 Playlists 导航、kiosk 非空集合、中文说明精确值及其他语言回退已补回归。
- RGUI 中英文与德文回退、Ozone／XMB／MaterialUI 的入口和导入导航通过。
- 从集合实际运行 2048 核心，二次确认关闭后 CONTENTLESS，返回条目页，后退回原集合。
- 缺内容／缺核心不启动；未指定核心进入原生选择。
- 新功能沿用原设置，关闭后是否退出尊重用户显式配置。

## 调度记录

真实工具 functions.subagent_codex 实现与修复、functions.subagent_claude_code 首审与复审均已实际调用并完成；没有以普通 agent 冒充。调用 ID、错误、测试证据见 progress.md。

修复轮次：1/2。选定 P3-1/3/4/5/6 已关闭。正式复审快照19文件、完整diff及未跟踪路径在主 Agent 最后核对时全部一致。

## 保留事项

- Linux／MinGW／无线程及其他端没有可用验证环境，未声称通过。
- P3-2 Make 命令解析仍有格式假设；待真实平台验证时改进，避免本轮无依据重写。
- 新测试尚未接入 samples 自动发现或 CI；后续配置发布流水线时接入。
- 测试失败路径的栈边界防护可完善，当前通过路径及产品代码不受影响，作为非阻断诊断改进延期。
- 实际多 ROM 扫描、实体手柄／触控、音频、完整视觉与发行资源未验收。
- 在线资源源、元数据、完整品牌／封面首页不属于本批实现。

## 文档

- 设计：docs/superpowers/specs/2026-11-02-native-library-design.md。
- 交付与复现：docs/development/native-library-delivery.md。
- 独立验收产物：artifacts/lead-acceptance（仅本地，不入库）。
