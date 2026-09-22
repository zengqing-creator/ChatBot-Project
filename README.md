AI聊天助手 (ChatBot)——一个基于C++和Web前端的多模态聊天应用，支持多会话管理、人设定制、语音合成与实时语音通话

一.功能特性：
1.多会话管理：创建、切换、删除独立聊天会话
2.AI对话：基于阿里云百炼大模型，支持流式回复与工具调用
3.人设系统：自定义AI角色Prompt，保存至数据库
4.长期记忆：基于向量检索的历史消息摘要与语义搜索
5.语音合成：将AI回复转为语音播放
6.实时语音通话：浏览器语音识别+WebSocket+TTS实现对话
7.本地化存储：SQLite数据库保存会话与消息

二.技术栈：
后端      C++17, cpp-httplib (含SSL), SQLite3, nlohmann/json, FAISS, Intel MKL
前端	  Vue 3 (CDN), TailwindCSS (CDN)
AI服务	阿里云百炼 DashScope API
内网穿透	ngrok

三.项目技术详解
（1）整体架构
前端
index.html(Vue3 + TailwindCSS) 会话列表、聊天窗口、输入框等；Web Speech API语音识别；Audio API播放TTS
| HTTP(8080),WebSocket(8081)
▼
C++后端
HTTP Server(httplib)     WebSocket Server(httplib,独立线程)
        |                               |
        ▼                               ▼
  Conversation.cpp(会话上下文) messages(json数组),system_prompt/summart
                                  |
                                  ▼
                  SQLiteDB.cpp chat_sessions,chat_messages
                                  |
                                  ▼
 utils.cpp call_ai_with_tools(LLM对话),get_embedding(向量化),synthesize_speech(TTS)
                                  | HTTPS
                                  ▼
          阿里云百炼DashScope API:Chat Completions(qwen-turbo)
                                 Embeddings(text-embedding-v4)
                                 TTS(qwen-audio-3.0-tts-flash)
（3）目录结构
ChatBot/
    ├── CMakeLists.txt          # 构建配置
    ├── README.md
    ├── start.bat               # 一键启动脚本（启动 exe + ngrok）
    ├── faiss.dll               # FAISS 运行库
    ├── include/                # 头文件
    │   ├── common.h            # 公共头（平台宏 + 依赖）
    │   ├── Conversation.h/.cpp # 会话 / 记忆压缩逻辑
    │   ├── SQLiteDB.h/.cpp     # 数据库 + 向量检索
    │   ├── utils.h/.cpp        # LLM 调用 / embedding / TTS
    │   ├── httplib.h / json.hpp / sqlite3.h / IndexFlat.h ...
    ├── src/
    │   ├── main.cpp            # 服务入口 + 路由
    │   ├── index.html          # 前端单页应用
    │   ├── memory.js           # （历史遗留，当前未接入）前端记忆模块
    │   ├── sqlite3.c           # SQLite 源码
    │   └── generate_token.py   # LiveKit 通话 token 生成
    ├── signal-server.js        # WebSocket 信令服务器（语音通话）
    └── lib/                    # 依赖库

（2）HTTP服务层
    选用httplib.h实现HTTP/HTTPS功能和跨平台，同时支持SSL的内置流式响应。
    以下是路由设计
    方法	      路径	            职责
    GET	                 /	        前端入口，返回index.html
    GET	            /api/sessions	    列出所有会话
    POST	    /api/sessions	    创建新会话
    GET	            /api/history	    拉取会话历史
    GET	            /api/summary	    拉取会话摘要
    POST	    /api/chat	            处理聊天消息
    POST	    /api/personality  	    修改人设
    POST	    /api/clear	            清空记忆
    POST	    /api/delete_session	    删除会话
    POST	    /api/tts	            语音合成
（3）数据库层
1.选用SQLite3嵌入式数据库，无需独立部署，数据以单文件形式随程序存放，启动即用。
2.设计两张核心表：
      会话表chat_sessions，以session_id为主键，保存人设提示词system_prompt、长期摘要summary及创建/更新时间。
      消息表chat_messages，以msg_id为主键，记录 session_id、角色user/assistant、内容、所属回合号turn_id与时间戳。
      向量表message_embeddings：以msg_id为主键，存储对应消息的向量BLOB。
3.在(session_id, turn_id)上建立索引，加速"按会话拉取历史"的查询。
4.所有SQL均使用占位符绑定参数，杜绝字符串拼接，天然防注入。
5.回合号turn_id而非时间戳作为排序依据，保证顺序稳定、不会因同一秒内的多条消息而错乱。
6.数据库连接由shared_ptr持有，主线程与语音线程共享同一连接，配合互斥锁串行化读写。

（4）会话上下文管理
1.每个会话在内存中维护一份messages数组，结构完全兼容OpenAI消息格式，并始终以system消息打头，携带人设与摘要。
     上下文采用滑动窗口和滚动摘要策略控制长度：
     设定上限约15轮对话，超出时截取最早的若干条旧消息；
     调用LLM将这些旧消息连同旧摘要一起压缩成一份新摘要，写回数据库的summary字段；
     从内存messages中删除这些旧消息，并重新刷新system消息，把新摘要注入人设末尾。
2.摘要的生成是递归式滚动更新——旧的摘要参与新的摘要生成，避免历史信息丢失。
3.每次会话初始化时，先读人设与摘要，再从数据库加载最近若干条历史消息，共同组成完整上下文。

（5）LLM 调用
所有AI能力统一走阿里云百炼DashScope的OpenAI兼容接口，后端只做代理，不承担模型推理。
对话采用流式模式SSE，边接收边拼接，前端可获得"打字机"效果。
请求体包含模型名、温度、重复惩罚、存在惩罚、最大token、工具定义等参数，temperature偏高以鼓励自然叙事，重复惩罚用于抑制车轱辘话。
SSE解析的关键在于跨分片缓冲：TCP字节流可能把一条事件切成几段，也可能一次送来多条，因此维护一个字符串缓冲，循环截取完整事件后再解析JSON，直到遇到[DONE]标志结束。
支持工具调用Function Calling：模型若判断需要外部信息，会返回tool_calls，后端根据工具名查表执行对应函数，把结果作为工具消息追加到上下文，再次请求模型生成最终回复。
迭代最多三次，避免"调用—返回—再调用"的死循环。
另有一个同步版call_llm_sync，专供摘要生成等不需要流式的场景使用，走非流式接口，一次拿回完整回复。
API Key、Endpoint、路径等敏感配置均从环境变量读取，不在代码中硬编码。

（6）长期记忆
1.后端使用FAISS做语义检索。每条消息通过Embeddings接口映射为高维向量，语义相近的文本在向量空间中彼此靠近。检索时把用户问句向量化，与当前会话内的历史向量逐一计算相似度，取分数最高的若干条作为"相关记忆"。
2.向量持久化在message_embeddings表。用户消息与AI回复落库时同步保存向量。
3.每轮对话前，后端对用户问句做一次embedding，调用searchSimilar检索Top-3；相似度低于0.30的条目直接丢弃，宁可空着也不把噪声灌进prompt。
4.检索结果以system消息形式插在system与历史消息之间，附加"仅供参考，不要原样复述"的说明。
5.代价是每轮对话产生3次embedding调用，配额消耗相对较高。

记忆由三层构成，全部存储在SQLite（chatbot.db）中
    | 表 | 作用 |
    |chat_sessions| 会话/角色：人设system_prompt、摘要summary|
    |chat_messages| 每条消息（含session_id、role、content、turn_id） |
    |message_embeddings| 每条消息的向量（含session_id，用于按会话隔离检索） |
工作流程：
1.用户发送消息 → 消息写入chat_messages，并调用text-embedding-v4生成向量写入message_embeddings（记录所属session_id）。
2. 回复前，用当前用户消息的向量在当前会话的向量库中做相似度检索（FAISS 内积，top-3），把命中的「相关记忆」作为 system 消息注入提示词。
3. 消息数量超过阈值（max_client_messages，默认15条×2）时触发trim_messages()：把最旧的若干条消息交给LLM生成摘要存入summary，并从上下文和数据库中裁剪。
4. 下次加载会话时，摘要会拼接到system prompt，保证长期记忆不丢失。

（7）语音合成
TTS 走DashScope的SpeechSynthesizer接口，提交文本与音色参数，返回一个可播放的音频URL。
前端在朗读前会先清洗文本：去除括号旁白、Markdown标记、多余空白，把换行替换为句号，并截断超长内容，保证朗读自然流畅。
播放使用浏览器原生Audio对象，播放中禁用按钮，结束后自动恢复状态。
音色与业务空间ID均通过环境变量配置，便于切换。

（8）语音通话
通话建立在WebSocket之上，运行于独立的8081端口，与HTTP服务并存但互不干扰。
前端使用浏览器Web Speech API做语音识别，识别结果为文本后，通过WebSocket发送给后端。
后端收到文本，走与文字聊天一致的对话逻辑，得到回复后再调用TTS拿到音频URL，一并通过WebSocket返回。
关键状态是"AI 正在说话"标志：AI播放音频期间暂停语音识别，播放结束后重新启动，避免麦克风录到AI的声音形成回声死循环。
浏览器兼容性上，语音识别目前仅Chrome与Edge支持。
WebSocket服务通过独立线程承载，主线程专注HTTP，二者共享同一份数据库与互斥锁。

（9）前端架构
前端为单页应用，使用Vue 3的响应式系统驱动视图，TailwindCSS负责样式，均通过CDN引入，无构建步骤。
核心状态包括：会话列表、当前会话ID、消息数组、输入内容、加载状态、各类模态框开关、通话状态等，全部用 ref 声明，修改即自动触发重渲染。
交互流程上：进入页面拉取会话列表，默认选中第一个；切换会话时拉取该会话的历史记录并替换消息数组；发送消息时先乐观地把用户消息推入界面，再请求后端，得到回复后追加显示。
输入框支持Enter发送、Shift+Enter换行，通过keyup事件配合修饰符阻止默认换行，并在发送前trim首尾空白。
AI消息旁提供"播放语音"按钮，点击后请求TTS接口并播放。
通话按钮切换语音通话状态，配合WebSocket与语音识别完成整个闭环。

（10）并发与线程安全
httplib 的服务器是多线程模型，每个请求由一个独立工作线程处理，多个请求可能同时访问数据库。
SQLite3 的单一连接并非线程安全，因此所有涉及数据库的读写在进入临界区前都要先获取全局互斥锁，退出作用域时自动释放。
数据库对象用 shared_ptr 管理，主线程与语音线程各持一份引用，引用计数归零时才真正析构，保证生命周期安全。
语音线程以按值方式捕获 shared_ptr，即使主线程先行退出，语音线程仍持有有效引用。
线程间通过互斥锁串行化对共享资源的访问，避免竞争条件。

四.工具
                                 版本                          下载链接
Visual Studio 2022 Build Tools  最新版  	Microsoft https://visualstudio.microsoft.com/zh-hans/downloads/
CMake	                         ≥3.21	            https://cmake.org/download/
OpenSSL	                         3.x	            cmd:vcpkg install openssl:x64-windows
ngrok                         	最新版	            https://ngrok.com/download/windows

五.准备工作
（1）获取API密钥
      访问阿里云百炼控制台https://bailian.console.aliyun.com/，登录/注册后点击API_Key创建，
      复制生成的API Key,在Windows下配置环境变量DASHSCOPE_API_KEY;
（2）获取业务空间ID(Workspace ID)
      访问千问AI平台https://platform.qianwenai.com/home/settings/workspaces获取Workspace ID并配置成DASHSCOPE_WORKSPACE_ID;
（3）获取音色ID(Voice ID)
      访问https://help.aliyun.com/zh/model-studio/cosyvoice-tts-http-api，利用声音设计或声音复刻功能创建自定义音色，创建成功后系统会返回一个形如“qwen-audio-3.0-tts-flash-myvoice-xxxxxx”的id，配置成DASHSCOPE_VOICE_ID;
      
六.编译命令
   cd /d "D:\ChatBot Project\ChatBot"
   rmdir /s /q build
   mkdir build
   cd build
   cmake .. -G "Visual Studio 17 2022" -A x64
   cmake --build . --config Release
   编译完成后运行D:\ChatBot Project\ChatBot\start.bat，脚本会自动运行程序和内网穿透ngrok，将8080端口暴露到公网，供远程设备访问
   注意：必须使用MSVC构建。本项目依赖FAISS和Intel MKL，二者只提供MSVC版本的库，用MinGW/GCC会在链接阶段报undefined reference

      
