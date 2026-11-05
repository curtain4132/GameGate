# 编写 GameGate 静态游戏源（JSON v1）

状态：JSON v1及Windows原生源管理流程已实现并完成本地验收，尚未正式发布；Android native/设备验证待补。功能和已接受限制见[开发交付记录](../development/static-sources-delivery.md)，设计要求及偏差见[静态源设计](../superpowers/specs/2026-11-04-static-game-sources-design.md)。

游戏源是一个通过HTTPS发布的UTF-8 JSON文件。它列出游戏资料和下载信息，不是模拟器核心包，也不意味着GameGate或源维护者拥有对应游戏的分发许可。

## 最小目录

以下目录合法地表示一个尚未收录游戏的源：

```json
{
  "schema_version": 1,
  "source": {
    "id": "my-homebrew-library",
    "name": "我的自制游戏目录"
  },
  "games": []
}
```

source.id 是源声明的稳定身份。更换名称可以保留ID；刷新时若 source.id 改变，客户端拒绝该次更新并保留旧缓存。需要切换成另一个源时，应由用户移除旧登记后重新添加。GameGate还会为用户添加的地址分配独立本地实例ID，不直接用远端字符串作为文件路径。

根对象必须包含整数 `schema_version: 1`、`source` 对象和 `games` 数组；`1.0`、字符串 `"1"` 都不是本协议的版本值。`source.id`、`source.name` 必填且非空；`source.description` 和 HTTPS `source.homepage` 可省略。每款游戏必须提供 `id`、`platform`、非空 `original_name`。可选字段缺失时直接省略键，不用 JSON `null` 代替字符串或对象。

## 增加一款游戏

```json
{
  "schema_version": 1,
  "source": {
    "id": "my-homebrew-library",
    "name": "我的自制游戏目录"
  },
  "games": [
    {
      "id": "sample-gb-world-v1",
      "platform": "gb",
      "original_name": "Sample Game",
      "original_language": "en",
      "localized": {
        "en": {
          "name": "Sample Game",
          "description": "An illustrative entry, not an actual download."
        },
        "zh-Hans": {
          "name": "示例游戏",
          "description": "用于说明格式的虚构游戏。"
        }
      },
      "region": "world",
      "version": "1.0"
    }
  ]
}
```

目录和游戏的ID均为1–128个ASCII字母、数字、点、下划线或短横线。一个源内游戏ID必须唯一，不能因为名称相同就把不同平台或不同文件版本合并为同一条。

## 平台与资料语言

| 代码 | 初始识别的平台 |
|---|---|
| nes | FC／NES |
| gb | Game Boy |
| gbc | Game Boy Color |
| gba | Game Boy Advance |
| snes | SFC／SNES |
| megadrive | Mega Drive／Genesis |

未知平台代码可以保留，但不会被判定为已兼容或自动获得可用核心。

localized使用BCP47语言标签，例如en、ja、zh-Hans、zh-Hant，不能写成 `zh_CN`。语言标签按ASCII大小写不敏感匹配，同一游戏不能同时声明 `zh-Hans` 和 `ZH-hans`。客户端做精确标签匹配，不自动把地区标签缩短成语言标签。

名称与简介分别寻找首选语言、英文、原始语言，名称最后回退original_name，简介没有对应内容时显示暂无资料。例如首选 `zh-Hans`，中文仅提供名称、英文仅提供简介时，会组合中文名称和英文简介，并分别保留命中的资料语言。本地化名称/简介的空字符串按未提供处理；缺少译文不能使整个游戏条目消失。

不要把游戏发行地区当成资料语言，也不要将简繁转换当成地区官方译名。源提供的中文名称和介绍会标识其来源，不会自动变成权威元数据。

## 下载信息和外部资料ID

游戏对象可以带download：

```json
{
  "url": "https://example.org/games/sample.gb",
  "size_bytes": 40960,
  "sha256": "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
}
```

上例地址和摘要都是示意值，不能直接用于真实下载。download缺失表示未提供下载地址；download存在时url必填。大小和SHA-256可省略，大小0与未知大小不同。size_bytes须是0至18446744073709551615之间的十进制整数，不能用负数、小数、指数或字符串形式；SHA-256存在时必须是64位十六进制。

本批客户端只展示下载信息，游戏文件下载、入库和核心安装属于后续功能。不要用目录成功加载来宣称游戏已经安装。

外部资料ID使用提供者名称作为命名空间，例如 `"external_ids": {"igdb": "12345"}`。提供者键使用与ID相同的ASCII字符集；值必须是非空字符串，不能把 `12345` 写成JSON数字。同一游戏最多32个提供者，同名提供者键不能重复。这些ID不会取代GameGate内部身份，本批也不会自动请求对应资料服务。

游戏可带 `"cover_url": "https://example.org/images/sample.png"`。它只保存图片地址，本批不下载、缓存或渲染远端封面。

## 托管和刷新

- 将目录放在有有效证书的HTTPS地址，地址不得带用户名密码或片段。
- 所有URL均为ASCII绝对HTTPS地址；非ASCII域名使用punycode，非ASCII路径使用百分号编码。homepage、cover_url、download.url同样遵守该规则。
- 查询参数会按原意保留，不通过排序或删减改变地址含义；当前版本按完整URL字节精确去重，不规范化主机大小写、默认端口或路径。
- 最多跟随5次301/302/303/307/308跳转，每一跳仍须HTTPS且无用户名密码、片段；传输总截止时间为30秒。
- 建议直接提供未压缩JSON或gzip。deflate的不同封装在系统后端上可能有差异，本批未增加兼容解压回退。
- 源刷新是整体校验后替换，异常目录不会覆盖上一次成功缓存。
- 不需要提供JavaScript或执行代码；客户端不会执行源中的脚本。
- 本批不支持Cookie登录、任意请求头或交互式鉴权。
- 源维护者应保持条目ID稳定，不用列表下标当ID。

## 大小限制

| 项目 | 上限 |
|---|---:|
| 目录JSON | 4 MiB |
| 游戏条目 | 5000 |
| JSON嵌套深度 | 16 |
| 每个游戏的本地化语言 | 16 |
| ID、platform及外部ID提供者键 | 128 ASCII字节 |
| 名称 | 256 UTF-8字节 |
| 简介 | 8192 UTF-8字节 |
| URL | 2048字节 |
| 语言标签 | 63 ASCII字节 |
| region、version | 当前实现为128 UTF-8字节 |
| 每个游戏的外部ID提供者 | 32 |
| 外部ID值 | 256 UTF-8字节 |

中文字符通常占多个UTF-8字节，字节限制不等于字符数。字段类型错误、重复关键字段、重复ID、无效编码、未知schema_version和超限会使本次刷新失败。所有字符串和键均不得包含NUL（包括转义后的 `\u0000`）。未识别扩展字段可以跳过，但仍受整体字节数和深度限制。region/version的空字符串目前保留为空值，编写源时建议直接省略未提供的字段。

## 开发测试建议

先用空目录和一个自制测试条目验证界面，再依次测试中文名称、缺失简介、未知平台和目录更新。把错误JSON、重复ID和断网作为独立场景，不用来源不明的商业ROM作为协议测试依赖。

发布前核对游戏与图片是否允许分发；元数据库查询许可、图片缓存许可和游戏文件分发许可是不同事项。
