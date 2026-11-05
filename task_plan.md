# GameGate 当前开发计划：GG-002 静态 JSON 游戏源

## 已完成历史

GG-001 原生游戏库已提交为 73100fda362b8622981daa2163cff1bc2d6a39cb，完成Codex实现/修复、主Agent验收、Claude复审。历史记录见 progress.md 和 docs/development/native-library-delivery.md。

## 目标与批准

2026-11-04 用户确认 docs/superpowers/specs/2026-11-04-static-game-sources-design.md。完成源注册、HTTPS有界刷新、目录与详情、多语言、缓存、停用/移除。不是只做解析器；最终需交付菜单端到端流程。不实现游戏下载、核心安装和外部资料服务，不推送远端。

## 基线

- 起始分支feat/native-game-library，HEAD 73100fda362b8622981daa2163cff1bc2d6a39cb。
- tracked diff和暂存均为空；仅有主Agent新增的GG-002设计文档未跟踪。
- 准备新分支feat/static-game-sources承接既有功能。
- 保留业务源码、字体修复的便携产物和用户配置；不回退已有改动。

## 串行阶段

1. [进行中] GG-002A：目录模型、严格JSON解析、验证、稳定身份和逐字段多语言回退；聚焦真实C测试。
2. [待执行] A主Agent独立验收、Claude只读审查、必要修复闭环。
3. [待执行] GG-002B：注册存储、有界HTTPS获取、取消/代次、缓存替换与失败保留。
4. [待执行] B独立验收、Claude审查及必要修复。
5. [待执行] GG-002C：原生源管理/目录/详情与中英文文案。
6. [待执行] C独立验收、端到端真实交互、最终集成审查、更新交付说明。

## 调度规则

已读取 codex-claude-review。真实工具 functions.subagent_codex 独占业务源码/测试实现和修复；functions.subagent_claude_code只读审查；主Agent负责文档、基线、独立验收和问题裁决。普通subagent只做只读研究。当前无GG-001遗留写者。

任务链修复计数0/2，不能通过拆包重置。各包实际调用ID、写域和结果记录progress.md。

A写域：新增gamegate/catalog.c/h、samples/gamegate/catalog/（构建脚本与C测试），必要的本包小范围编译接线需说明；禁止改菜单/核心/playlist/libretro-common、既有harness、配置凭据、主Agent文档。设计审查前不允许进入B或C。

主Agent文档写域：task_plan.md/findings.md/progress.md、docs/superpowers/specs、docs/development交付记录。实现者不得修改。

## 验证资源

- Python：C:/Users/lenovo/.dsh/dsh-runtimes/dsh-primary-runtime/dependencies/python/python.exe。
- VS2022 BuildTools MSBuild：C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe，v143。
- 产物：artifacts/static-sources/，主验收独立子目录lead/。
- Linux/MinGW此前未安装；本轮检查后如仍无环境，明确未验证，不安装绕过。
- tests必须执行真实parser/model行为；协议错误、UTF8、重复字段/ID、各类限额、URL、语言fallback等有可复现断言。
- 网络传输必须在接收中限额，不能仅在全部读入后判断；超时、重定向、证书验证和退出取消必须有明确实现。
- 无线程模式任务必须分片推进，不因接入HTTP任务接口就假定天然非阻塞。

## 当前错误/阻碍

GG-002A实现任务 d662e539-9b3e-47fd-84e4-7b048e8ba820 连续三个目标回合仍无完成/失败/已停止确认，catalog.c/h及测试目录尚不存在。当前工具未提供可查询或取消该外部实现任务的接口；job_list不包含它，不能将无列表记录当作已停止。另一个只读探测已成功，说明并非已证实的后端整体不可用，因此不能据此降级接管或重新分配同一写域。

暂停受影响实现与后续依赖包，等待原任务的终态通知，或用户在子任务面板确认其已完成/已取消并停止后继续。具体依据：codex-claude-review要求重新分配写权限前确认旧写者结束。已有设计、实施计划和网络只读发现保留，未修改业务代码、提交或推送。便携字体视觉确认不属于此阻塞。
