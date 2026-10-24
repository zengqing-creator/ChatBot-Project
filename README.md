# ChatBot 多模态聊天助手

> 基于 C++ 和 Web 前端的多模态聊天应用，支持多会话管理、角色人设定制、长期记忆、语音合成与实时语音通话。

## 一、功能特性

1. 多会话管理：创建、切换、删除独立聊天会话。
2. AI 对话：基于阿里云百炼大模型，支持流式回复与工具调用。
3. 人设系统：自定义 AI 角色 Prompt（角色扮演 / 剧情叙事），保存至数据库。
4. 长期记忆：基于向量检索的历史消息摘要与语义搜索。
5. 语音合成：将 AI 回复转为语音播放。
6. 实时语音通话：浏览器语音识别 + WebSocket + TTS 实现对话。
7. 本地化存储：SQLite 数据库保存会话与消息。
8. 用户认证：基于 Supabase 的邮箱注册 / 登录，会话与数据按用户隔离。

## 二、技术栈

| 层级 | 技术 |
| --- | --- |
| 后端 | C++17、cpp-httplib（含 SSL）、SQLite3、nlohmann/json、FAISS、Intel MKL、OpenSSL |
| 前端 | Vue 3（CDN）、TailwindCSS（CDN） |
| AI 服务 | 阿里云百炼 DashScope API |
| 用户认证 | Supabase（邮箱登录 + JWT 本地离线验签） |
| 内网穿透 | ngrok |

## 三、项目技术详解

### （1）整体架构

```text
前端
index.html（Vue3 + TailwindCSS）：会话列表、聊天窗口、输入框等；Web Speech API 语音识别；Audio API 播放 TTS
        │
        │ HTTP(8080)、WebSocket(8081)
        ▼
C++ 后端
HTTP Server（httplib）                 WebSocket Server（httplib，独立线程）
        │                                       │
        ▼                                       ▼
Conversation.cpp（会话上下文）          messages（JSON 数组）、system_prompt / summary
        │
        ▼
SQLiteDB.cpp（chat_sessions、chat_messages、message_embeddings）
        │
        ▼
utils.cpp：call_ai_with_tools（LLM 对话）、get_embedding（向量化）、synthesize_speech（TTS）
        │ HTTPS
        ▼
阿里云百炼 DashScope API
  - Chat Completions（qwen-turbo）
  - Embeddings（text-embedding-v4）
  - TTS（qwen-audio-3.1-tts-flash）
```

### （2）目录结构

```text
ChatBot/
├── CMakeLists.txt          # 构建配置（含 FAISS / MKL 路径）
├── README.md
├── start.bat               # 一键启动脚本（编译 + 启动 exe + ngrok）
├── Clone and TTS.py        # 声音复刻脚本（上传 OSS 音频后填入 URL，运行得到 voice_id）
├── faiss.dll               # FAISS 运行库
├── signal-server.js        # 历史遗留的 Node 信令服务器（语音通话已内置到 exe，无需单独启动）
├── package.json            # Node 依赖（ws），仅供 signal-server.js 使用
├── include/                # 头文件（第三方库 + 自有模块声明）
│   ├── common.h            # 公共头（平台宏 + 依赖）
│   ├── Auth.h              # Supabase JWT 验证（OpenSSL EC P-256 验签）
│   ├── Conversation.h      # 会话 / 记忆压缩接口（实现见 src/Conversation.cpp）
│   ├── SQLiteDB.h          # 数据库 + 向量检索接口（实现见 src/SQLiteDB.cpp）
│   ├── utils.h             # LLM 调用 / embedding / TTS 接口（实现见 src/utils.cpp）
│   ├── httplib.h           # cpp-httplib（HTTP / HTTPS / WebSocket）
│   ├── json.hpp            # nlohmann/json
│   ├── sqlite3.h           # SQLite 头文件
│   ├── IndexFlat.h         # FAISS 索引
│   ├── livekit.h           # LiveKit C++ SDK（第三方，当前未接入编译）
│   └── room.h              # LiveKit room 定义（同上，未使用）
├── src/
│   ├── main.cpp            # 服务入口 + HTTP 路由 + WebSocket 语音服务
│   ├── index.html          # 前端单页应用（Vue3 + Supabase 登录）
│   ├── jwks.json           # Supabase JWKS 公钥（后端验签 JWT 用，构建时拷贝到 exe 目录）
│   ├── Conversation.cpp    # 会话 / 记忆压缩实现
│   ├── SQLiteDB.cpp        # 数据库 + 向量检索实现
│   ├── utils.cpp           # LLM / embedding / TTS 实现
│   ├── sqlite3.c           # SQLite 源码（单文件合并版）
│   ├── memory.js           # 历史遗留（浏览器端本地记忆），当前未接入
│   ├── generate_token.py   # 历史遗留：LiveKit 通话 token 生成
│   ├── testAPIKey.cpp      # 测试：HTTP 客户端 + JSON 解析
│   ├── test_env.cpp        # 测试：环境变量读取
│   └── output/             # 空目录（历史遗留）
├── build/                  # CMake 构建产物（含 bin/Release/ChatBot.exe，脚本自动生成）
├── cmake/                  # CMake 辅助脚本（LiveKitSDK.cmake，未接入）
├── drogon/                 # 历史遗留（Drogon 框架，未使用）
├── node_modules/           # Node 依赖（ws）
├── venv/                   # Python 虚拟环境（供 generate_token.py）
└── lib/                    # 依赖库（sqlite3.dll）
```

### （3）HTTP 服务层

选用 `httplib.h` 实现 HTTP/HTTPS 功能和跨平台支持，同时支持 SSL 的内置流式响应。

路由设计如下：

| 方法 | 路径 | 职责 |
| --- | --- | --- |
| GET | `/` | 前端入口，返回 `index.html` |
| GET | `/api/sessions` | 列出所有会话 |
| POST | `/api/sessions` | 创建新会话 |
| GET | `/api/history` | 拉取会话历史（最近 50 条） |
| GET | `/api/summary` | 拉取会话摘要 |
| POST | `/api/chat` | 处理聊天消息 |
| POST | `/api/personality` | 修改人设 |
| POST | `/api/clear` | 清空记忆 |
| POST | `/api/delete_session` | 删除会话 |
| POST | `/api/tts` | 语音合成 |

> 以上所有 API 均需在请求头携带 `Authorization: Bearer <token>`，后端通过 Supabase 校验 token，并据此按用户隔离数据；未登录或 token 过期将返回 401。
>
> 语音通话走独立的 WebSocket 通道 `ws://<host>:8081/voice?token=<access_token>`（详见「（9）语音通话」），不走上述 HTTP 路由；token 以查询参数传入，后端同样做 JWT 校验。

### （4）数据库层

1. 选用 SQLite3 嵌入式数据库，无需独立部署，数据以单文件形式随程序存放，启动即用。
2. 设计核心表：
   - 会话表 `chat_sessions`：以 `session_id` 为主键，保存人设提示词 `system_prompt`、长期摘要 `summary` 及创建/更新时间。
   - 消息表 `chat_messages`：以 `msg_id` 为主键，记录 `session_id`、角色 `user/assistant`、内容、所属回合号 `turn_id` 与时间戳。
   - 向量表 `message_embeddings`：以 `msg_id` 为主键，存储对应消息的向量 BLOB。
3. 在 `(session_id, turn_id)` 上建立索引，加速“按会话拉取历史”的查询。
4. 所有 SQL 均使用占位符绑定参数，杜绝字符串拼接，天然防注入。
5. 使用回合号 `turn_id` 而非时间戳作为排序依据，保证顺序稳定，不会因同一秒内的多条消息而错乱。
6. 数据库连接由 `shared_ptr` 持有，主线程与语音线程共享同一连接，配合互斥锁串行化读写。
7. 三张核心表均含 `user_id` 列，所有查询均以 `user_id` 过滤，实现多用户数据隔离。
8. 启动时执行 `migrateEmbeddingsTable()`：检测旧版 `message_embeddings` 表是否缺少 `session_id` 列，缺则自动补列并回填、清理孤儿向量，保证老库平滑升级。

### （5）会话上下文管理

1. 每个会话在内存中维护一份 `messages` 数组，结构完全兼容 OpenAI 消息格式，并始终以 `system` 消息打头，携带人设与摘要。上下文采用滑动窗口和滚动摘要策略控制长度：
   - 设定上限约 15 轮对话（`MAX_TURNS = 15`，`MAX_MESSAGES = 15 * 2 + 1`），超出时截取最早的若干条旧消息；
   - 调用 LLM 将这些旧消息连同旧摘要一起压缩成一份新摘要，写回数据库的 `summary` 字段；
   - 从内存 `messages` 中删除这些旧消息，并重新刷新 `system` 消息，把新摘要注入人设末尾。
2. 摘要的生成是递归式滚动更新——旧的摘要参与新的摘要生成，避免历史信息丢失。
3. 摘要累计到一定批量（`summary_batch = 8` 条）才触发一次 LLM 摘要；若摘要生成失败，则回退为“原文片段拼接”，保证记忆不因一次失败而丢失。
4. 每次会话初始化时，先读人设与摘要，再从数据库加载最近若干条历史消息，共同组成完整上下文。

### （6）LLM 调用

- 所有 AI 能力统一走阿里云百炼 DashScope 的 OpenAI 兼容接口，后端只做代理，不承担模型推理。
- 对话采用流式模式 SSE，边接收边拼接，前端可获得“打字机”效果。
- 请求体固定使用 `qwen-turbo`，参数为 `temperature = 0.75`、`repetition_penalty = 1.2`、`presence_penalty = 0.6`、`max_tokens = 2000`；`temperature` 偏高以鼓励自然叙事，重复惩罚用于抑制车轱辘话。
- SSE 解析的关键在于跨分片缓冲：TCP 字节流可能把一条事件切成几段，也可能一次送来多条，因此维护一个字符串缓冲，循环截取完整事件后再解析 JSON，直到遇到 `[DONE]` 标志结束。
- 支持工具调用 Function Calling：模型若判断需要外部信息，会返回 `tool_calls`，后端根据工具名查表执行对应函数，把结果作为工具消息追加到上下文，再次请求模型生成最终回复。
- 目前内置一个工具 `get_current_time`：当用户询问“现在几点 / 今天几号”时，模型返回该工具调用，后端执行并回填当前时间。
- 迭代最多三次，避免“调用—返回—再调用”的死循环。
- 另有一个同步版 `call_llm_sync`，专供摘要生成等不需要流式的场景使用，走非流式接口，一次拿回完整回复。
- API Key、Endpoint、路径等敏感配置均从环境变量读取，不在代码中硬编码。

### （7）长期记忆

1. 后端使用 FAISS 做语义检索。每条消息通过 Embeddings 接口映射为高维向量，语义相近的文本在向量空间中彼此靠近。检索时把用户问句向量化，与当前会话内的历史向量逐一计算相似度，取分数最高的若干条作为“相关记忆”。
2. 向量持久化在 `message_embeddings` 表。用户消息与 AI 回复落库时同步保存向量。
3. 每轮对话前，后端对用户问句做一次 embedding，调用 `searchSimilar` 检索 Top-3（FAISS `IndexFlatIP` 内积）；相似度低于 `0.30` 的条目直接丢弃，宁可空着也不把噪声灌进 prompt。
4. 检索结果以 `system` 消息形式插在 system 与历史消息之间，附加“仅供参考，不要原样复述”的说明。
5. 代价是每轮对话产生 2 次 embedding 调用（用户消息 1 次、AI 回复 1 次），配额消耗相对较高。

记忆由三层构成，全部存储在 SQLite（`chatbot.db`）中：

| 表 | 作用 |
| --- | --- |
| `chat_sessions` | 会话/角色：人设 `system_prompt`、摘要 `summary` |
| `chat_messages` | 每条消息（含 `session_id`、`role`、`content`、`turn_id`） |
| `message_embeddings` | 每条消息的向量（含 `session_id`，用于按会话隔离检索） |

工作流程：

1. 用户发送消息 → 消息写入 `chat_messages`，并调用 `text-embedding-v4` 生成向量写入 `message_embeddings`（记录所属 `session_id`）。
2. 回复前，用当前用户消息的向量在当前会话的向量库中做相似度检索（FAISS 内积，Top-3），把命中的“相关记忆”作为 `system` 消息注入提示词。
3. 消息数量超过阈值（默认 15 轮 × 2）时触发 `trim_messages()`：把最旧的若干条消息交给 LLM 生成摘要存入 `summary`，并从上下文和数据库中裁剪。
4. 下次加载会话时，摘要会拼接到 `system prompt`，保证长期记忆不丢失。

### （8）语音合成

- TTS 走 DashScope 的 `SpeechSynthesizer` 接口，提交文本与音色参数，返回一个可播放的音频 URL。
- 请求模型为 `qwen-audio-3.1-tts-flash`（**当前 TTS 为 3.1 版本**），输出 `mp3`、采样率 24kHz；接口路径 `/api/v1/services/audio/tts/SpeechSynthesizer`。
- 自定义音色必须针对同一模型（3.1）复刻，否则 voice_id 与模型不匹配，合成会报 `InvalidParameter / Engine error`。
- 前端在朗读前会先清洗文本：去除括号旁白、Markdown 标记、多余空白，把换行替换为句号，并截断超长内容，保证朗读自然流畅。
- 播放使用浏览器原生 `Audio` 对象，播放中禁用按钮，结束后自动恢复状态。
- 音色与业务空间 ID 均通过环境变量配置，便于切换。

### （9）语音通话

- 通话建立在 WebSocket 之上，运行于独立的 `8081` 端口，与 HTTP 服务并存但互不干扰。
- 前端使用浏览器 Web Speech API 做语音识别，识别结果为文本后，通过 WebSocket 发送给后端。
- 后端收到文本，走与文字聊天一致的对话逻辑，得到回复后再调用 TTS 拿到音频 URL，一并通过 WebSocket 返回。
- 关键状态是“AI 正在说话”标志：AI 播放音频期间暂停语音识别，播放结束后重新启动，避免麦克风录到 AI 的声音形成回声死循环。
- 浏览器兼容性上，语音识别目前仅 Chrome 与 Edge 支持。
- WebSocket 服务通过独立线程承载，主线程专注 HTTP，二者共享同一份数据库与互斥锁。
- 消息协议为 JSON：客户端发 `{"type":"chat","session_id":"...","text":"..."}` 发起对话、发 `{"type":"ping"}` 做心跳；服务端回 `{"type":"reply","text":"...","audio_url":"..."}`、`{"type":"pong"}` 或 `{"type":"error","message":"..."}`。
- 连接时通过查询参数 `?token=<access_token>` 鉴权，无效 token 直接关闭连接。

### （10）前端架构

- 前端为单页应用，使用 Vue 3 的响应式系统驱动视图，TailwindCSS 负责样式，均通过 CDN 引入，无构建步骤。
- 核心状态包括：会话列表、当前会话 ID、消息数组、输入内容、加载状态、各类模态框开关、通话状态等，全部用 `ref` 声明，修改即自动触发重渲染。
- 交互流程：
  - 进入页面拉取会话列表，默认选中第一个；
  - 切换会话时拉取该会话的历史记录并替换消息数组；
  - 发送消息时先乐观地把用户消息推入界面，再请求后端，得到回复后追加显示。
- 输入框支持 Enter 发送、Shift+Enter 换行，通过 `keyup` 事件配合修饰符阻止默认换行，并在发送前 `trim` 首尾空白。
- AI 消息旁提供“播放语音”按钮，点击后请求 TTS 接口并播放。
- 通话按钮切换语音通话状态，配合 WebSocket 与语音识别完成整个闭环。
- 登录 / 注册通过 `@supabase/supabase-js`（CDN）完成，登录成功后把 `access_token` 注入每个 API 请求头（详见「（12）用户认证」）。

### （11）并发与线程安全

- `httplib` 的服务器是多线程模型，每个请求由一个独立工作线程处理，多个请求可能同时访问数据库。
- SQLite3 的单一连接并非线程安全，因此所有涉及数据库的读写在进入临界区前都要先获取全局互斥锁，退出作用域时自动释放。
- 数据库对象用 `shared_ptr` 管理，主线程与语音线程各持一份引用，引用计数归零时才真正析构，保证生命周期安全。
- 语音线程以按值方式捕获 `shared_ptr`，即使主线程先行退出，语音线程仍持有有效引用。
- 线程间通过互斥锁串行化对共享资源的访问，避免竞争条件。

### （12）用户认证（Supabase + JWT）

- 认证统一托管在 Supabase Auth，前端通过 `@supabase/supabase-js` 完成邮箱注册 / 登录，成功后拿到 `access_token`（JWT）。
- 前端在每次 HTTP 请求的 `Authorization: Bearer <access_token>` 头（语音 WebSocket 则放在 `?token=` 查询参数）中携带该 token。
- 后端**不访问 Supabase 在线接口**，而是在本地离线验签：启动时从 `jwks.json` 读取 Supabase 项目的 JWKS 公钥（EC P-256），用 OpenSSL 的 `ECDSA_verify` 对 JWT 的 ES256 签名做校验。
- 验签流程（见 `include/Auth.h`）：
  1. 拆出 JWT 的 header / payload / signature 三段；
  2. 校验 header 中 `alg` 必须为 `ES256`；
  3. 对 `header.payload` 做 SHA-256 摘要，用 JWKS 公钥验签（将 JWS 的 `r||s` 原始签名转成 OpenSSL 所需的 DER 格式）；
  4. 校验 payload 中的 `exp`（过期时间），通过后取 `sub` 字段作为 `user_id`。
- 校验结果按 token 缓存约 300 秒（`cache_`），避免每请求重复验签的开销。
- 所有数据库读写都以 `user_id` 作为过滤条件，实现按用户的数据隔离；不同账号之间看不到彼此的会话与消息。

## 四、工具

| 工具 | 版本 | 下载/安装 |
| --- | --- | --- |
| Visual Studio 2022 Build Tools | 最新版 | [Microsoft](https://visualstudio.microsoft.com/zh-hans/downloads/) |
| CMake | ≥ 3.21 | [https://cmake.org/download/](https://cmake.org/download/) |
| OpenSSL | 3.x | `vcpkg install openssl:x64-windows` |
| ngrok | 最新版 | [https://ngrok.com/download/windows](https://ngrok.com/download/windows) |

## 五、准备工作

### （1）获取 API 密钥

访问阿里云百炼控制台：<https://bailian.console.aliyun.com/>，登录/注册后点击 `API_Key` 创建，复制生成的 API Key，在 Windows 下配置环境变量 `DASHSCOPE_API_KEY`。

### （2）获取业务空间 ID（Workspace ID）

访问千问 AI 平台：<https://platform.qianwenai.com/home/settings/workspaces>，获取 Workspace ID，并配置成 `DASHSCOPE_WORKSPACE_ID`。

### （3）获取音色 ID（Voice ID）

> 当前项目 TTS 统一使用 **3.1 版本**模型 `qwen-audio-3.1-tts-flash`。自定义音色必须针对同一模型复刻，否则填入的 voice_id 在合成时会报错（`InvalidParameter / Engine error`）。

获取自定义音色有两种方式：

**方式一：控制台手动创建**

访问：<https://help.aliyun.com/zh/model-studio/cosyvoice-tts-http-api>，利用声音设计或声音复刻功能创建自定义音色。创建成功后，系统会返回一个形如 `qwen-audio-3.1-tts-flash-myvoice-xxxxxx` 的 ID，配置成 `DASHSCOPE_VOICE_ID`。

**方式二：用 `Clone and TTS.py` 脚本复刻**

1. **准备声音样本并上传 OSS**：准备一段声音样本（MP3），上传到阿里云对象存储 OSS 的 bucket，并把该对象设为**公共读**（或使用带签名的 URL），得到公网可访问的 MP3 地址，形如 `https://<你的bucket>.oss-cn-<region>.aliyuncs.com/<文件>.mp3`。
2. **修改脚本参数**：打开 `ChatBot/Clone and TTS.py`，改两处：
   - 顶部 `workspace_id = "ws-xxxx"` 改成你自己的业务空间 ID（见「（2）获取业务空间 ID」）；
   - 请求体里的 `"audio": "https://..."` 改成上一步得到的 MP3 地址；
   - （可选）`"prefix": "myvoice"` 可改成你喜欢的音色命名前缀。
3. **配置 API Key**：确保已配置环境变量 `DASHSCOPE_API_KEY`（脚本通过 `os.environ['DASHSCOPE_API_KEY']` 读取）。
4. **运行脚本**：`python "Clone and TTS.py"`。脚本会调用声音复刻接口创建音色并轮询等待就绪，最后打印 `voice_id`（形如 `qwen-audio-3.1-tts-flash-myvoice-xxxxxx`）。
5. **使用 voice_id**：把打印出的 `voice_id` 填入环境变量 `DASHSCOPE_VOICE_ID` 作为默认音色，或在创建角色时填入「专属音色 ID」字段。

> 脚本中的 `target_model` 已固定为 `qwen-audio-3.1-tts-flash`，与项目合成模型保持一致，请勿改动，否则生成的 voice_id 无法被当前项目使用。
>
> 脚本 `base_url` 中的 `cn-beijing` 为地域段；若你的业务空间不在北京地域，需相应修改该地域。

### （4）配置 Supabase（用户认证）

本项目用户登录 / 注册托管在 Supabase，需要自行创建项目并配置两处：

1. 在 [Supabase](https://supabase.com/) 注册并新建项目，得到项目 URL（形如 `https://<ref>.supabase.co`）与 `anon` key。
2. 打开 `src/index.html`，把其中的 `SUPABASE_URL` 与 `SUPABASE_ANON_KEY` 两个常量改成你自己项目的值。
3. 获取该项目的 JWKS 公钥并保存为 `src/jwks.json`：浏览器访问 `https://<ref>.supabase.co/auth/v1/.well-known/jwks.json`，把返回的 JSON 整体保存（后端取第一个 key，须为 `EC` / `P-256` / `ES256`）。构建时 CMake 会自动把 `jwks.json` 拷贝到 exe 同目录。
4. 在 Supabase 控制台的 Authentication → Providers 中开启 Email 的注册与“确认邮件”功能，否则邮箱注册流程无法完成。

> 注意：`SUPABASE_URL`、`SUPABASE_ANON_KEY`（`index.html`）与 `jwks.json` 必须来自**同一个** Supabase 项目，否则后端验签必然失败、登录后仍返回 401。

### （5）环境变量清单

项目运行时需要以下环境变量（缺一不可，程序通过 `std::getenv` 读取）：

| 环境变量 | 说明 | 示例 |
| --- | --- | --- |
| `DASHSCOPE_API_KEY` | 阿里云百炼 API Key | `sk-xxxxxxxxxxxxxxxx` |
| `DASHSCOPE_WORKSPACE_ID` | 业务空间 ID（Workspace ID） | `llm-xxxxxxxx` |
| `DASHSCOPE_VOICE_ID` | 音色 ID | `qwen-audio-3.1-tts-flash-myvoice-xxxxxx` |

> 说明：`API_ENDPOINT`（`https://dashscope.aliyuncs.com`）与 `API_PATH`（`/compatible-mode/v1/chat/completions`）在 `utils.cpp` 中写死，无需配置。
>
> 补充：`DASHSCOPE_WORKSPACE_ID` 会被读取到 `WORKSPACE_ID` 变量，但当前对话 / embedding / TTS 请求均未使用该值，配置与否暂不影响运行。

Windows 下配置示例（PowerShell，写入用户级环境变量，永久生效）：

```powershell
[Environment]::SetEnvironmentVariable("DASHSCOPE_API_KEY", "sk-xxxx", "User")
[Environment]::SetEnvironmentVariable("DASHSCOPE_WORKSPACE_ID", "llm-xxxx", "User")
[Environment]::SetEnvironmentVariable("DASHSCOPE_VOICE_ID", "qwen-audio-3.1-tts-flash-myvoice-xxxxxx", "User")
```

配置完成后需重启终端 / IDE 使环境变量生效。

## 六、编译命令

假设项目文件下载到D盘，修改并保存代码之后直接运行 `D:\ChatBot Project\ChatBot\start.bat`，脚本会自动编译运行和内网穿透 ngrok，将 `8080` 端口暴露到公网，供远程设备访问

> 注意：必须使用 MSVC 构建。本项目依赖 FAISS 和 Intel MKL，二者只提供 MSVC 版本的库，用 MinGW/GCC 会在链接阶段报 `undefined reference`。

## 七、快速开始（运行指南）

1. **安装工具**：Visual Studio 2022 Build Tools、CMake ≥ 3.21、OpenSSL 3.x、ngrok（见「四、工具」）。
2. **核对依赖路径**：`CMakeLists.txt` 中 FAISS 与 MKL 路径为硬编码，默认 `FAISS_ROOT = C:/dev/faiss-install`、`MKL_ROOT = C:/Program Files (x86)/Intel/oneAPI/mkl/2025.3`，与本机不一致时需先改掉再编译。
3. **配置环境变量**：按「五、准备工作」配置 `DASHSCOPE_API_KEY`、`DASHSCOPE_WORKSPACE_ID`、`DASHSCOPE_VOICE_ID`，并重启终端。
4. **配置 Supabase 认证**：按「五、准备工作（4）」配置 `src/index.html` 中的 `SUPABASE_URL` / `SUPABASE_ANON_KEY`，并放置 `src/jwks.json`。
5. **一键启动**：运行 `D:\ChatBot Project\ChatBot\start.bat`，脚本会依次完成 CMake 配置、Release 编译、启动 `ChatBot.exe`、启动 ngrok 把 `8080` 端口暴露到公网。（脚本内项目目录与 ngrok 路径为硬编码，若项目不在 `D:\ChatBot Project\ChatBot` 请先修改 `start.bat`。）
6. **访问与登录**：
   - 本机访问：<http://localhost:8080>
   - 局域网访问：`http://<本机局域网IP>:8080`（后端监听 `0.0.0.0:8080`）
   - 公网访问：使用 ngrok 分配的 URL
   - 首次使用需在登录界面用邮箱注册（Supabase），去邮箱点击确认链接后再登录。
7. **语音通话**：登录后点击通话按钮即可。语音通道由 `ChatBot.exe` 内置的 WebSocket 服务提供（`ws://<主机>:8081/voice`），无需单独启动 `signal-server.js`。

> 后端启动时会打印 `AI Web Server 已启动！` 与 `语音通话服务已启动: ws://0.0.0.0:8081/voice`，看到这两行即表示服务就绪。

## 八、常见问题（FAQ）

**Q1：编译 / 链接报 `undefined reference`？**
本项目依赖 FAISS 与 Intel MKL，二者只提供 MSVC 版本库。请使用 Visual Studio 2022 构建，不要用 MinGW/GCC。

**Q2：链接报找不到 `faiss.lib` / `mkl_*.lib`？**
`CMakeLists.txt` 中 `FAISS_ROOT`、`MKL_ROOT` 为硬编码路径，与本机安装位置不一致时会找不到库。改成你自己的路径后删除 `build/` 重新运行 CMake。

**Q3：对话返回「找不到 API_KEY 环境变量」？**
说明 `DASHSCOPE_API_KEY` 未配置或未生效。按「五、准备工作」配置后，重启终端 / IDE 再启动。

**Q4：访问页面提示 401 Unauthorized？**
所有 API 都要求携带 `Authorization: Bearer <token>` 头。确认已登录；若登录过期，重新登录即可（前端会自动带上 access_token）。

**Q5：语音通话没声音 / 连不上 8081？**
- 语音识别依赖浏览器 Web Speech API，目前仅 Chrome / Edge 支持；
- 确认 `ChatBot.exe` 已在监听 8081（启动日志会打印对应提示）；
- 未登录时不会建立语音通道，请先登录。

**Q6：ngrok 无法启动？**
`start.bat` 中 ngrok 路径写死为 `C:\Users\z'q\AppData\Local\Microsoft\WindowsApps\ngrok.exe`，请改成本机 ngrok 的实际路径。

**Q7：网页能打开但会话为空 / 数据不保存？**
数据保存在 exe 同目录的 `chatbot.db`。请确认 exe 运行目录有写入权限，且未误删 `chatbot.db`。

**Q8：启动时报「无法加载 jwks.json」？**
后端启动时会在 exe 同目录读取 `jwks.json` 用于验签 JWT。确认已按「五、准备工作（4）」获取并放置该文件；构建时 CMake 会自动拷贝，手动运行 exe 时请确保 `jwks.json` 与 exe 同目录。

**Q9：登录成功但访问仍提示 401 / token 验证失败？**
多半是 `jwks.json` 与 `index.html` 中的 Supabase 项目不匹配。请确认 `SUPABASE_URL`、`SUPABASE_ANON_KEY` 与 `jwks.json` 来自同一个 Supabase 项目；更换项目后需重新获取 JWKS。
