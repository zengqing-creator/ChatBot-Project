#include "Conversation.h"
#include "SQLiteDB.h"
#include "utils.h"

Conversation::Conversation() : messages(json::array()), session_id(""), summary(""), db(nullptr) {
    messages = json::array();
}

void Conversation::setDisplayName(const std::string& name) { 
    display_name = name; 
}

std::string Conversation::getDisplayName() const { 
    return display_name; 
}

void Conversation::changePersonality(const std::string& prompt) {
    if(db) {
        db->setSystemPrompt(session_id, prompt);
        loadSystemPrompt();
        std::cout << "[系统] 已更新为: " << prompt << std::endl;
    }
}

std::string Conversation::GetCurrentPersonality() {
    if(db) {
        return db->getRawSystemPrompt(session_id);
    }
    return "";
}

void Conversation::initDB(std::shared_ptr<SQLiteDB> database, const std::string& sid) {
    db = database;
    session_id = sid;
    db->ensureSession(session_id);
    summary = db->getSummary(session_id);

    if(messages.empty()){
        messages = json::array();
    }

    loadSystemPrompt();
    loadFromDatabase();
}

void Conversation::loadSystemPrompt() {
    if(!db) return;

    std::string prompt = db->getSystemPrompt(session_id);
    if(!summary.empty())prompt += summary;

    int system_index = -1;
    for (size_t i = 0; i < messages.size(); i++) {
        if (messages[i]["role"] == "system") {
            system_index = i;
            break;
        }
    }
    
    if (system_index >= 0)
        messages[system_index]["content"] = prompt;
    else {
        json system_msg;
        system_msg["role"] = "system";
        system_msg["content"] = prompt;
        if (messages.empty())
            messages = json::array({system_msg});
        else
            messages.insert(messages.begin(), system_msg);
    }
}

void Conversation::trim_messages() {
    int max_messages = max_client_messages * 2 + 1;
    if ((int)messages.size() > max_messages) {
        int remove_count = (int)messages.size() - max_messages;
        json old_messages = json::array();
        for (int i = 1;i < 1 + remove_count && i < (int)messages.size();i++) {
            old_messages.push_back(messages[i]);
        }
        if(!old_messages.empty()) {
            std::string new_summary = generateSummary(old_messages, summary);
            if(!new_summary.empty()) {
                summary = new_summary;
                db->setSummary(session_id, summary);
            }
            //若生成失败，则保留最后一条消息为摘要
            else {
                for(int i = old_messages.size() - 1;i >= 0;--i) {
                    if(old_messages[i]["role"] == "user") {
                        summary = "[最后用户说]" + old_messages[i]["content"].get<std::string>();
                        db->setSummary(session_id, summary);
                        break;
                    }
                }
            }
        }
        messages.erase(messages.begin() + 1, messages.begin() + remove_count + 1);
        loadSystemPrompt();
        if(db) db->deleteOldMessages(session_id, max_messages);
    }
}

void Conversation::loadFromDatabase() {
    if (!db) return;
    
    auto history = db->getRecentMessages(session_id, max_client_messages * 2);
    for (const auto& [role, content] : history) {
        messages.push_back({{"role", role}, {"content", content}});
    }
    loaded = true;
    
    std::cout << "[系统] 已从数据库加载 " << history.size() << " 条历史消息" << std::endl;
}

void Conversation::add_user_message(const std::string& content) {
    messages.push_back({{"role", "user"}, {"content", content}});
    trim_messages();
    
    if (db) {
        int turn_id = db->getMaxTurnId(session_id) + 1;
        db->saveMessage(session_id, "user", content, turn_id);
    }
}

void Conversation::add_assistant_message(const std::string& content) {
    messages.push_back({{"role", "assistant"}, {"content", content}});
    trim_messages();
    
    if (db) {
        int turn_id = db->getMaxTurnId(session_id);
        db->saveMessage(session_id, "assistant", content, turn_id);
    }
}

json Conversation::get_messages() const {
    return messages;
}

void Conversation::print_history() const {
    if(!summary.empty())
        std::cout << "历史摘要: " << summary << std::endl;
    
    std::string ai_name = display_name.empty() ? "AI" : display_name;
    for (const auto& msg : messages) {
        std::string role = msg["role"];
        std::string content = msg["content"];
        if (role == "system") continue;
        else if (role == "user")
            std::cout << "用户: " << content << std::endl;
        else if (role == "assistant") {
            std::cout << ai_name << "：" << content << std::endl;
        }
    }
}

std::string Conversation::generateSummary(const json& old_messages, const std::string& old_summary) {
    std::string prompt = "请根据先前摘要和最新对话，生成一份详细完整的新摘要（不少于200字），包括主要事件、人物情感变化和关键对话。\n";
    if(!old_summary.empty())
        prompt += "先前摘要: " + old_summary + "\n";
    prompt += "最新对话:\n";

    const int MAX_SUMMARY_MESSAGES = 20;
    int count = 0;
    for(int i = (int)old_messages.size() - 1;i >= 0  && count < MAX_SUMMARY_MESSAGES;--i, ++count) {
        const auto& msg = old_messages[i];
        std::string role = msg["role"];
        std::string content = msg["content"].get<std::string>();
        if (role == "user")
            prompt += "用户: " + content + "\n";
        else if (role == "assistant")
            prompt += display_name + "：" + content + "\n";
    }
    return call_llm_sync(prompt);
}

void Conversation::appendSystemContext(const std::string& extra) {
    if (extra.empty()) return;

    for (auto& m : messages) {
        if (m["role"] == "system") {
            std::string cur = m.value("content", "");
            m["content"] = cur.empty() ? extra : (cur + "\n\n" + extra);
            return;
        }
    }
    json sys_msg = {
        {"role", "system"},
        {"content", extra}
    };
    messages.insert(messages.begin(), sys_msg);
}

std::string Conversation::getSessionId() const {
    return session_id;
}