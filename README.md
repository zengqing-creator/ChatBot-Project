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
后端	  C++17, cpp-httplib (含SSL), SQLite3, nlohmann/json
前端	  Vue 3 (CDN), TailwindCSS (CDN)
AI服务	阿里云百炼 DashScope API
内网穿透	ngrok

三.项目技术详解
（1）整体架构
前端
index.html(Vue3 + TailwindCSS) 会话列表、聊天窗口、输入框等；Web Speech API语音识别；Audio API播放TTS
| HTTP(8080),HTTPS(8081)
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
（2）HTTP服务层
    选用httplib.h实现HTTP/HTTPS功能和跨平台，同时支持SSL的内置流式响应。
    以下是路由设计
    方法	      路径	            职责
    GET	         /	        前端入口，返回index.html
    GET	    /api/sessions	    列出所有会话
    POST	  /api/sessions	      创建新会话
    GET	    /api/history	    拉取会话历史
    GET	    /api/summary	    拉取会话摘要
    POST	  /api/chat	        处理聊天消息
    POST	  /api/personality  	修改人设
    POST	  /api/clear	        清空记忆
    POST	  /api/delete_session	删除会话
    POST	  /api/tts	          语音合成
    POST	  /api/embed	        文本向量化


三.工具
                                 版本                          下载链接
Visual Studio 2022 Build Tools  最新版  	Microsoft https://visualstudio.microsoft.com/zh-hans/downloads/
CMake	                           ≥3.21	            https://cmake.org/download/
OpenSSL	                          3.x	            cmd:vcpkg install openssl:x64-windows
ngrok                         	最新版	            https://ngrok.com/download/windows

四.准备工作
（1）获取API密钥
      访问阿里云百炼控制台https://bailian.console.aliyun.com/，登录/注册后点击API_Key创建，
      复制生成的API Key,在Windows下配置环境变量DASHSCOPE_API_KEY
（2）

      
