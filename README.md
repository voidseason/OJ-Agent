# OJ智能辅助

> **架构**: C++ 做 LLM 调用基座，Python 做智能体（Agent）

---

## C++ 部分 — LLM 调用基座

### 项目结构

```
ChatSDK_LLM/                    # LLM SDK —— 核心库
├── SDK/
│   ├── include/
│   │   ├── ChatSDK.h           # 顶层入口 Engine 类
│   │   ├── LLMManager.h        # LLM 模型管理
│   │   ├── LLMProvider.h       # Provider 抽象接口
│   │   ├── UnifiedProvider.h   # 统一 Provider（兼容 OpenAI / DeepSeek 等多 API 风格）
│   │   ├── SessionManager.h    # 会话管理
│   │   ├── DataManager.h       # 数据持久化（SQLite）
│   │   ├── common.h            # 公共数据结构（Message / Session / Config 等）
│   │   └── sqlite3.h
│   ├── src/                    # 实现文件
│   │   ├── ChatSDK.cpp / DataManager.cpp / SessionManager.cpp
│   │   ├── LLMManager.cpp / LLMProvider.cpp / UnifiedProvider.cpp
│   │   ├── log.cpp
│   │   └── httplib.h           # HTTP 客户端（cpp-httplib）
│   └── util/
│       ├── log.h               # spdlog 封装
│       └── nocopy.h
│
├── Test/                       # Google Test 单元测试
│   ├── CMakeLists.txt
│   ├── test_ChatSDK.cpp
│   └── test_UnifiedProvider.cpp

grpc_server/                    # gRPC 服务端 —— 暴露远程调用接口
├── CMakeLists.txt
├── include/
│   └── ChatServiceImpl.h
├── src/
│   ├── main.cc                 # 服务入口
│   └── ChatServiceImpl.cpp     # 服务实现

proto/                          # 协议定义
└── ChatService.proto
```

### 依赖

| 依赖 | 用途 |
|------|------|
| spdlog | 日志 |
| jsoncpp | JSON 解析 |
| SQLite3 | 数据持久化（模型配置、会话历史） |
| OpenSSL | HTTPS 支持 |
| cpp-httplib | HTTP 客户端（已内嵌于 SDK 源码） |
| gRPC + Protobuf | 远程过程调用（仅 grpc_server） |
| GTest | 单元测试（仅 Test） |

### 快速开始

#### 1. 编译并运行 gRPC 服务端

```bash
cd grpc_server
cmake -B build
cmake --build build
./build/bin/chat_grpc_server
```

#### 2. 在你的项目中使用 ChatSDK

```cpp
#include "ChatSDK.h"

ChatSDK::Engine engine("chat.db");
engine.Initialize();

// 添加模型
engine.AddModel("deepseek", {
    {"api_key",  "sk-xxx"},
    {"model",    "deepseek-chat"},
    {"endpoint", "https://api.deepseek.com"}
});

// 创建会话并发送消息
auto sid = engine.CreateSession("deepseek");
auto reply = engine.SendMessage(sid, "你好，请帮我解释一下这段代码");
```

#### 3. 运行测试

```bash
cd ChatSDK_LLM/Test
cmake -B build
cmake --build build
./build/test_ChatSDK
```

### SDK 核心架构

```
┌─────────────┐
│   Engine    │  ← 顶层入口，对外暴露统一接口
├─────────────┤
│ LLMManager  │  ← 管理多个 LLM Provider，处理消息路由
├─────────────┤
│UnifiedProvider│  ← 统一 HTTP API 调用（兼容 OpenAI/DashScope 等）
├─────────────┤
│SessionManager│  ← 管理会话与消息历史
├─────────────┤
│ DataManager │  ← SQLite 持久化层
└─────────────┘
```

**支持的特性：**
- 多模型管理：动态增删 LLM 模型
- 会话管理：创建、删除、列出会话，支持历史消息回溯
- 消息发送：**全量返回**（`SendMessage`）和**流式返回**（`SendMessageStream`，实时打字机效果）
- 统一 Provider：一套接口兼容 OpenAI `/v1/chat/completions` 和 `/v1/responses` 等不同 API 风格
- 数据持久化：模型配置与会话消息自动保存到 SQLite，重启后自动恢复
- **gRPC 远程调用**：通过 gRPC 暴露以上所有能力，供 Python Agent 或其他客户端使用

### gRPC 服务接口

定义在 [proto/ChatService.proto](proto/ChatService.proto) 中，主要 RPC：

| RPC | 说明 |
|-----|------|
| `Ping` | 心跳检测 |
| `AddModel / RemoveModel / ListModels` | 模型生命周期管理 |
| `CreateSession / DeleteSession / ListSessions / GetMessages` | 会话管理 |
| `SendMessage` | 非流式消息发送 |
| `StreamMessage` | **流式消息发送**（Server-Sent Streaming） |

---

## Python 部分 — Agent（待开发）

> Python 端通过 gRPC 客户端与 C++ 服务端通信，实现智能体逻辑。

```
Agent/          ← 智能体代码（开发中）
├── ...
```
