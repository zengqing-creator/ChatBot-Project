#include "utils.h"
#include "Conversation.h"
#include "SQLiteDB.h"
#include <cstdlib>
#include <algorithm>
#include <iostream>

//API配置
extern const std::string API_KEY = std::getenv("DASHSCOPE_API_KEY");
extern const std::string API_ENDPOINT = "https://dashscope.aliyuncs.com";
extern const std::string API_PATH = "/compatible-mode/v1/chat/completions";
extern const std::string WORKSPACE_ID = std::getenv("DASHSCOPE_WORKSPACE_ID");
extern const std::string VOICE_ID = std::getenv("DASHSCOPE_VOICE_ID");


//编码转换
std::string gbk_to_utf8(const std::string& gbk_str) {
    int wide_len = MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, nullptr, 0);
    std::wstring wide_str(wide_len, L'\0');
    MultiByteToWideChar(CP_ACP, 0, gbk_str.c_str(), -1, wide_str.data(), wide_len);

    int utf8_len = WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string utf8_str(utf8_len, '\0');
    WideCharToMultiByte(CP_UTF8, 0, wide_str.c_str(), -1, utf8_str.data(), utf8_len, nullptr, nullptr);
    return utf8_str;
}

//工具函数
std::string get_current_time(const std::string& format) {
    time_t now = time(nullptr);
    struct tm* local_time = localtime(&now);
    char buffer[256];
    if (format == "YYYY-MM-DD HH:MM:SS") {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    } else if (format == "HH:MM:SS") {
        strftime(buffer, sizeof(buffer), "%H:%M:%S", local_time);
    } else if (format == "YYYY-MM-DD") {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d", local_time);
    } else {
        strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", local_time);
    }
    return std::string(buffer);
}

std::map<std::string, std::function<std::string(const json&)>> tool_functions = {
    {"get_current_time", [](const json& args) -> std::string {
        std::string format = args.value("format", "YYYY-MM-DD HH:MM:SS");
        return get_current_time(format);
    }}
};

json get_tools_definition() {
    return json::array({
        {
            {"type", "function"},
            {"function", {
                {"name", "get_current_time"},
                {"description", "获取当前日期和时间。当用户询问现在几点、今天几号、当前时间等问题时调用此函数。"},
                {"parameters", {
                    {"type", "object"},
                    {"properties", {
                        {"format", {
                            {"type", "string"},
                            {"enum", {"YYYY-MM-DD HH:MM:SS", "HH:MM:SS", "YYYY-MM-DD"}},
                            {"description", "时间格式，默认为 YYYY-MM-DD HH:MM:SS"}
                        }}
                    }},
                    {"required", {}}
                }}
            }}
        }
    });
}


//核心对话函数
std::string call_ai_with_tools(Conversation& conv, std::function<void(const std::string&)> on_chunk) {
    if (API_KEY.empty())
        return "错误：找不到 API_KEY 环境变量";

    json messages = conv.get_messages();
    std::string user_query;
    for (auto it = messages.rbegin(); it != messages.rend(); ++it) {
        if ((*it)["role"] == "user") {
            user_query = (*it)["content"].get<std::string>();
            break;
        }
    }

    bool should_continue = true;
    int max_iterations = 3;
    std::string final_answer;
    std::string accumulated_content;

    while (should_continue && max_iterations-- > 0) {
        accumulated_content.clear();
        
        json request_body = {
            {"model", "qwen-turbo"},
            {"messages", messages},
            {"temperature", 0.75},
            {"repetition_penalty", 1.2},
            {"presence_penalty", 0.6},
            {"max_tokens", 2000},
            {"stream", true},
            {"tools", get_tools_definition()},
            {"tool_choice", "auto"}
        };

        httplib::Client client(API_ENDPOINT);
        client.enable_server_certificate_verification(false);
        client.set_decompress(false);
        client.set_read_timeout(60, 0);

        httplib::Headers headers = {
            {"Authorization", "Bearer " + API_KEY},
            {"Accept", "text/event-stream"}
        };

        std::string finish_reason;
        json tool_calls_array = json::array();
        std::string stream_buffer;

        try {
            auto res = client.Post(API_PATH, headers, request_body.dump(), "application/json",
                [&](const char* data, size_t data_length) {
                    stream_buffer.append(data, data_length); 
                    
                    size_t pos = 0;
                    while ((pos = stream_buffer.find("data: ")) != std::string::npos) {
                        size_t end_pos = stream_buffer.find("\n", pos);
                        if (end_pos != std::string::npos) {
                            std::string json_str = stream_buffer.substr(pos + 6, end_pos - (pos + 6));
                            
                            if (!json_str.empty() && json_str.back() == '\r') {
                                json_str.pop_back();
                            }

                            if (!json_str.empty() && json_str != "[DONE]") {
                                try {
                                    json response = json::parse(json_str);
                                    if (response.contains("choices") && !response["choices"].empty()) {
                                        auto& choice = response["choices"][0];
                                        if (choice.contains("finish_reason") && !choice["finish_reason"].is_null())
                                            finish_reason = choice["finish_reason"].get<std::string>();
                                            
                                        if (choice.contains("delta")) {
                                            auto& delta = choice["delta"];
                                            
                                            if (delta.contains("content") && !delta["content"].is_null()) {
                                                std::string content = delta["content"].get<std::string>();
                                                accumulated_content += content;
                                                if (on_chunk) on_chunk(content);
                                            }
                                            
                                            if (delta.contains("tool_calls")) {
                                                for (auto& tc : delta["tool_calls"]) {
                                                    int index = tc["index"].get<int>();
                                                    auto it = std::find_if(tool_calls_array.begin(), tool_calls_array.end(),
                                                        [index](const json& j) { return j["index"].get<int>() == index; });
                                                    if (it == tool_calls_array.end()) {
                                                        json new_tc;
                                                        new_tc["index"] = index;
                                                        new_tc["id"] = tc.value("id", "");
                                                        new_tc["type"] = tc.value("type", "function");
                                                        new_tc["function"]["name"] = tc["function"].value("name", "");
                                                        new_tc["function"]["arguments"] = tc["function"].value("arguments", "");
                                                        tool_calls_array.push_back(new_tc);
                                                    } else {
                                                        if (tc["function"].contains("arguments"))
                                                            (*it)["function"]["arguments"] += tc["function"]["arguments"].get<std::string>();
                                                        if (tc.contains("id")) (*it)["id"] = tc["id"];
                                                        if (tc["function"].contains("name"))
                                                            (*it)["function"]["name"] = tc["function"]["name"];
                                                    }
                                                }
                                            }
                                        }
                                    }
                                } catch (const std::exception& e) {
                                    std::cerr << "JSON解析异常: " << e.what() << "\n内容: " << json_str << std::endl;
                                }
                            }
                            stream_buffer.erase(0, end_pos + 1);
                        } else {break;}
                    }
                    return true;
                }
            );

            if (accumulated_content.empty() && tool_calls_array.empty() && !stream_buffer.empty()) {
                std::cerr << "[后端调试] 收到未处理的异常数据: " << stream_buffer << std::endl;
            }

            if (!res) return "错误：网络请求失败（无响应）";
            if (res->status != 200) return "错误：HTTP " + std::to_string(res->status) + " - " + res->body;

            if (finish_reason == "tool_calls" && !tool_calls_array.empty()) {
                json assistant_msg;
                assistant_msg["role"] = "assistant";
                assistant_msg["content"] = accumulated_content.empty() ? nullptr : accumulated_content;
                assistant_msg["tool_calls"] = tool_calls_array;
                messages.push_back(assistant_msg);

                for (auto& tc : tool_calls_array) {
                    std::string tool_name = tc["function"]["name"];
                    json args;
                    try {
                        args = json::parse(tc["function"]["arguments"].get<std::string>());
                    } catch (...) { args = json::object(); }
                    std::string result;
                    auto it = tool_functions.find(tool_name);
                    result = (it != tool_functions.end()) ? it->second(args) : "错误：未知工具 " + tool_name;
                    json tool_msg;
                    tool_msg["role"] = "tool";
                    tool_msg["tool_call_id"] = tc["id"];
                    tool_msg["content"] = result;
                    messages.push_back(tool_msg);
                }
                should_continue = true;
            } else {
                final_answer = accumulated_content;
                should_continue = false;
            }
        } catch (const std::exception& e) {
            std::cerr << "错误: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "未知异常" << std::endl;
            return "错误：未知异常";
        }
    }

    if (final_answer.empty() && !accumulated_content.empty())
        final_answer = accumulated_content;
    return final_answer.empty() ? "（无响应内容）" : final_answer;
}

std::string call_llm_sync(const std::string& user_prompt) {
    if (API_KEY.empty()) return "";
    json messages = json::array();
    messages.push_back({{"role", "system"}, {"content", "你是一个专业的摘要生成助手。"}});
    messages.push_back({{"role", "user"}, {"content", user_prompt}});
    
    json request_body = {
        {"model", "qwen-turbo"},
        {"messages", messages},
        {"temperature", 0.75},
        {"repetition_penalty", 1.2},
        {"presence_penalty", 0.6},
        {"max_tokens", 2000},
        {"stream", false},
        {"tools", get_tools_definition()},
        {"tool_calling", "auto"}
    };

    httplib::Client client(API_ENDPOINT);
    client.enable_server_certificate_verification(false);
    client.set_read_timeout(60, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + API_KEY}
    };

    try {
        auto res = client.Post(API_PATH, headers, request_body.dump(), "application/json");
        if (!res || res->status != 200) return "";
            json response = json::parse(res->body);
            if (response.contains("choices") && !response["choices"].empty()) {
                auto& choice = response["choices"][0];
                if (choice.contains("message") && choice["message"].contains("content"))
                    return choice["message"]["content"].get<std::string>();
            }
    } catch (...) {}
    return "";
}

std::vector<float> get_embedding(const std::string& text) {
    if (API_KEY.empty()) return {};

    json request_body = {
        {"model", "text-embedding-v4"},
        {"input", {text}}
    };

    httplib::Client client(API_ENDPOINT);
    client.enable_server_certificate_verification(false);
    client.set_read_timeout(60, 0);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + API_KEY}
    };

    try {
        auto res = client.Post("/compatible-mode/v1/embeddings", headers, request_body.dump(), "application/json");
        if (res && res->status == 200) {
            json response = json::parse(res->body);
            if (response.contains("data") && !response["data"].empty()) {
                std::vector<float> emb;
                for (const auto& v : response["data"][0]["embedding"])
                    emb.push_back(v.get<float>());
                return emb;
            }
        }
    } catch (...) {}
    return {};
}

std::string create_cloned_voice(const std::string& api_key, 
                                const std::string& workspace_id,
                                const std::string& audio_url) {
    std::string host = workspace_id + ".cn-beijing.maas.aliyuncs.com";
    httplib::Client client(host);

    httplib::Headers headers = {
        {"Authorization", "Bearer " + api_key}
    };

    json request_body = {
        {"model", "voice-enrollment"},
        {"input", {
            {"action", "create_voice"},
            {"target_model", "qwen-audio-3.0-tts-flash"}, 
            {"prefix", "myvoice"},
            {"url", audio_url}
        }}
    };

    std::string body_str = request_body.dump();

    auto res = client.Post("/api/v1/services/audio/tts/customization", 
                           headers, body_str, "application/json");

    if (res && res->status == 200) {
        try {
            json response = json::parse(res->body);
            if (response.contains("output") && response["output"].contains("voice")) {
                std::string voice_id = response["output"]["voice"];
                std::cout << "音色创建成功! voice_id: " << voice_id << std::endl;
                return voice_id;
            }
        } catch (const std::exception& e) {
            std::cerr << "解析响应JSON失败: " << e.what() << std::endl;
        }
    } else {
        std::cerr << "创建音色失败，HTTP状态码: " << (res ? res->status : 0) << std::endl;
        if (res) std::cerr << "响应体: " << res->body << std::endl;
    }
    return "";
}

std::string synthesize_speech(const std::string& api_key,
                              const std::string& workspace_id,
                              const std::string& voice_id,
                              const std::string& text) {
    try {
        std::cerr << "[DEBUG] api_key 长度 = " << api_key.size() << std::endl;
        std::cerr << "[DEBUG] voice_id = " << voice_id << std::endl;

        httplib::Client client("https://dashscope.aliyuncs.com");
        client.enable_server_certificate_verification(false);
        client.set_read_timeout(60, 0);
        client.set_write_timeout(60, 0);

        httplib::Headers headers = {
            {"Authorization", "Bearer " + api_key}
        };

        json request_body = {
            {"model", "qwen-audio-3.0-tts-flash"},
            {"input", {
                {"text", text},
                {"voice", voice_id},
                {"format", "mp3"},
                {"sample_rate", 24000}
            }}
        };

        std::cerr << "[DEBUG] 发送请求..." << std::endl;
        auto res = client.Post("/api/v1/services/audio/tts/SpeechSynthesizer",
                               headers, request_body.dump(), "application/json");
        std::cerr << "[DEBUG] 返回状态码: " << (res ? std::to_string(res->status) : "no response") << std::endl;

        if (res && res->status == 200) {
            json response = json::parse(res->body);
            if (response.contains("output") && response["output"].contains("audio") &&
                response["output"]["audio"].contains("url")) {
                std::string audio_url = response["output"]["audio"]["url"];
                std::cout << "语音合成成功! 音频URL: " << audio_url << std::endl;
                return audio_url;
            }
        } else {
            std::cerr << "语音合成失败，HTTP状态码: " << (res ? res->status : 0) << std::endl;
            if (res) std::cerr << "响应体: " << res->body << std::endl;
        }
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
    }
    return "";
}