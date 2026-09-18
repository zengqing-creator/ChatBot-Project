#pragma once
#include "common.h"
#include <functional>

class Conversation;

extern const std::string API_KEY;
extern const std::string API_ENDPOINT;
extern const std::string API_PATH;
extern const std::string WORKSPACE_ID;
extern const std::string VOICE_ID;

std::string get_current_time(const std::string& format = "YYYY-MM-DD HH:MM:SS");
json get_tools_definition();
std::string call_ai_with_tools(Conversation& conv, std::function<void(const std::string&)> on_chunk = nullptr);

extern std::map<std::string, std::function<std::string(const json&)>> tool_functions;

std::string call_llm_sync(const std::string& prompt);

std::vector<float> get_embedding(const std::string& text);

std::string synthesize_speech(const std::string& api_key, const std::string& workspace_id,const std::string& voice_id, const std::string& text);