#include "common.h"
#include "SQLiteDB.h"
#include "Conversation.h"
#include "utils.h"
#include <fstream>
#include <mutex>
#include <thread>

std::mutex db_mutex;

int main() {
    std::cout << "Global API_KEY: " << API_KEY << std::endl;
    
    try {
        SetConsoleOutputCP(CP_UTF8);
        
        // 初始化数据库
        auto db = std::make_shared<SQLiteDB>("chatbot.db");
        httplib::Server svr;

        // 1. 路由：提供前端单页应用 (index.html)
        svr.Get("/", [](const httplib::Request& req, httplib::Response& res) {
            std::ifstream file("index.html");
            if (!file.is_open()) {
                res.status = 404;
                res.set_content("错误：未找到 index.html 文件！请确保它放置在可执行文件同级目录。", "text/plain; charset=utf-8");
                return;
            }
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            res.set_content(content, "text/html; charset=utf-8");
        });

        // 2. API：获取所有会话列表及最后一条消息
        svr.Get("/api/sessions", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                std::lock_guard<std::mutex> lock(db_mutex);
                auto sessions = db->getAllSessions(); //[cite: 1]
                json j_sessions = json::array();
                
                for (const auto& [id, prompt] : sessions) {
                    auto [last_msg, timestamp] = db->getLastMessageAndTime(id);    
                    j_sessions.push_back({
                        {"id", id},
                        {"prompt", prompt},
                        {"last_message", last_msg},
                        {"timestamp", timestamp}
                    });
                }

                res.set_content
                (j_sessions.dump(-1,' ',false,nlohmann::json::error_handler_t::replace), 
                "application/json; charset=utf-8");
            } catch (const std::exception& e) {
                std::cerr << "[后端报错]获取会话列表失败：" << e.what() << std::endl;
                res.status = 500;
                res.set_content("[]", "application/json; charset=utf-8");
            }
        });

        // 3. API：创建新会话
        svr.Post("/api/sessions", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string sid = body["session_id"];
                
                std::lock_guard<std::mutex> lock(db_mutex);
                db->ensureSession(sid);
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(e.what(), "text/plain");
            }
        });

        // 4. API：获取历史记录
        svr.Get("/api/history", [&db](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_param("session_id")) {
                res.status = 400;
                return;
            }
            std::string sid = req.get_param_value("session_id");
            
            std::lock_guard<std::mutex> lock(db_mutex);
            auto history = db->getRecentMessages(sid, 50); // 拉取最近50条
            json j_history = json::array();
            for (const auto& [role, content] : history) {
                j_history.push_back({{"role", role}, {"content", content}});
            }
            res.set_content(j_history.dump(), "application/json; charset=utf-8");
        });

        // 5. API：获取摘要内容
        svr.Get("/api/summary", [&db](const httplib::Request& req, httplib::Response& res) {
            if (!req.has_param("session_id")) {
                res.status = 400;
                return;
            }
            std::string sid = req.get_param_value("session_id");
            std::lock_guard<std::mutex> lock(db_mutex);
            std::string summary = db->getSummary(sid);
            json response = {{"summary", summary}};
            res.set_content(response.dump(), "application/json; charset=utf-8");
        });

        // 6. API：处理聊天信息
        svr.Post("/api/chat", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string sid = body["session_id"];
                std::string msg = body["message"];
                std::string mem_ctx  = body.value("memory_context", "");
                std::string emo_hint = body.value("emotion_hint", "");
                std::vector<std::string> prefs;
                if (body.contains("preferences") && body["preferences"].is_array())
                    for (auto& p : body["preferences"]) prefs.push_back(p.get<std::string>());

                Conversation conv;
                {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    conv.initDB(db, sid);
                    conv.setDisplayName(sid); 
                    conv.add_user_message(msg);
                }

                std::string extra;
                if (!mem_ctx.empty())  extra += mem_ctx + "\n";
                if (!emo_hint.empty()) extra += emo_hint + "\n";
                if (!prefs.empty()) {
                    extra += "【用户已说出口的偏好】\n";
                    for (auto& p : prefs) extra += "- " + p + "\n";
                }
                if (!extra.empty()) conv.appendSystemContext(extra);

                std::string full_response;
                std::string ret_msg = call_ai_with_tools(conv, [&](const std::string& c){ full_response += c; });
                if (full_response.empty() && !ret_msg.empty()) full_response = ret_msg;
                if (!full_response.empty()) {
                    std::lock_guard<std::mutex> lock(db_mutex);
                    conv.add_assistant_message(full_response);
                }

                json response = {{"reply", full_response}};
                res.set_content(response.dump(), "application/json; charset=utf-8");
            } catch (const std::exception& e) {
                res.status = 500;
                res.set_content(std::string("Internal Error: ") + e.what(), "text/plain");
            }
        });

        // 7. API：修改人设
        svr.Post("/api/personality", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string sid = body["session_id"];
                std::string prompt = body["prompt"];
                
                std::lock_guard<std::mutex> lock(db_mutex);
                db->setSystemPrompt(sid, prompt);
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } catch (...) {
                res.status = 400;
            }
        });

        // 8. API：清空记忆
        svr.Post("/api/clear", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string sid = body["session_id"];
                
                std::lock_guard<std::mutex> lock(db_mutex);
                db->deleteMessages(sid);
                res.set_content("{\"status\":\"ok\"}", "application/json");
            } catch (...) {
                res.status = 400;
            }
        });

        // 9. API：删除角色及其所有信息
        svr.Post("/api/delete_session", [&db](const httplib::Request& req, httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string sid = body["session_id"];
                
                std::lock_guard<std::mutex> lock(db_mutex);
                db->deleteSession(sid);
                res.set_content("{\"status\":\"ok\"}", "application/json; charset=utf-8");
            } catch (...) {
                res.status = 400;
                res.set_content("{\"status\":\"error\"}", "application/json; charset=utf-8");
            }
        });

        // 10. API：合成语音
        svr.Post("/api/tts", [](const httplib::Request& req,httplib::Response& res) {
            try {
                auto body = json::parse(req.body);
                std::string text = body.value("text", "");
                if (text.empty()) {
                    res.status = 400;
                    res.set_content("{\"error\":\"Missing text parameter\"}", "application/json");
                    return;
                }

                std::string audio_url = synthesize_speech(API_KEY, WORKSPACE_ID, VOICE_ID, text);
                if (audio_url.empty()) {
                    res.status = 500;
                    res.set_content("{\"error\":\"TTS synthesis failed\"}", "application/json");
                    return;
                }

                json response = {{"audio_url", audio_url}};
                res.set_content(response.dump(), "application/json; charset=utf-8");
            } catch (const std::exception& e) {
                res.status = 400;
                res.set_content(std::string("Error: ") + e.what(), "text/plain");
            }
        });

        // 11. API：获取文本嵌入向量
        svr.Post("/api/embed", [](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = json::parse(req.body);
            std::string text = body.value("text", "");
            if (text.empty()) { res.status = 400; return; }
            auto vec = get_embedding(text);
            res.set_content(json{{"embedding", vec}}.dump(), "application/json; charset=utf-8");
        } catch (...) { res.status = 400; }
    });

        std::cout << " AI Web Server 已启动！" << std::endl;
        std::cout << " 局域网访问地址: http://<你的局域网IP>:8080" << std::endl;

        // 语音通话线程
        std::thread voice_thread([db]() {
            httplib::Server ws_svr;

            ws_svr.WebSocket("/voice", [db](const httplib::Request&, httplib::ws::WebSocket& ws) {
                std::string msg;

                while (ws.is_open()) {
                    auto result = ws.read(msg);
                    if (result == httplib::ws::ReadResult::Fail) {
                        std::cout << "[语音通话] 连接关闭" << std::endl;
                        break;
                    }
                    if (result != httplib::ws::ReadResult::Text) continue;

                    try {
                        json body = json::parse(msg);
                        std::string type = body.value("type", "");

                        if (type == "chat") {
                            std::string sid = body.value("session_id", "");
                            std::string user_text = body.value("text", "");
                            if (sid.empty() || user_text.empty()) {
                                ws.send(json({{"type","error"},{"message","missing session_id or text"}}).dump());
                                continue;
                            }

                            std::cout << "[语音通话] 用户: " << user_text << std::endl;

                            Conversation conv;
                            {
                                std::lock_guard<std::mutex> lock(db_mutex);
                                conv.initDB(db, sid);
                                conv.setDisplayName(sid);
                                conv.add_user_message(user_text);
                            }

                            std::string reply = call_ai_with_tools(conv, nullptr);
                            if (reply.empty()) reply = "无响应内容";

                            {
                                std::lock_guard<std::mutex> lock(db_mutex);
                                conv.add_assistant_message(reply);
                            }

                            // 合成语音
                            std::cout << "[语音通话] AI: " << reply << std::endl;
                            std::string audio_url = synthesize_speech(API_KEY, WORKSPACE_ID, VOICE_ID, reply);
                            json response = {
                                {"type", "reply"},
                                {"text", reply},
                                {"audio_url", audio_url}
                            };
                            ws.send(response.dump());
                        }
                        else if (type == "ping") {
                            ws.send(json({{"type","pong"}}).dump());
                        }
                    } catch (const std::exception& e) {
                        ws.send(json({{"type","error"},{"message", e.what()}}).dump());
                    }
                }
            });

            std::cout << " 语音通话服务已启动: ws://0.0.0.0:8081/voice" << std::endl;
            ws_svr.listen("0.0.0.0", 8081);
        });
        voice_thread.detach();


        // 监听 0.0.0.0 以允许局域网设备访问，端口设为 8080
        svr.listen("0.0.0.0", 8080);
        
    } catch (const std::exception& e) {
        std::cerr << "致命错误: " << e.what() << std::endl;
        return 1;
    }
    return 0;
}