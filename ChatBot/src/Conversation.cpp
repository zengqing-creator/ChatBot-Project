#include "Conversation.h"
#include "SQLiteDB.h"
#include "utils.h"

Conversation::Conversation() : messages(json::array()), session_id(""), summary(""), db(nullptr) {}

void Conversation::setDisplayName(const std::string& name) { 
    display_name = name; 
}

void Conversation::initDB(std::shared_ptr<SQLiteDB> database, const std::string& sid) {
    db = database;
    session_id = sid;
    db->ensureSession(session_id, user_id);
    summary = db->getSummary(session_id, user_id);

    if(messages.empty()){
        messages = json::array();
    }

    loadSystemPrompt();
    loadFromDatabase();
}

void Conversation::loadSystemPrompt() {
    if(!db) return;

    std::string prompt = db->getSystemPrompt(session_id, user_id);
    if(!summary.empty())prompt += "\n\n【历史摘要】\n" + summary;

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
    if ((int)messages.size() <= MAX_MESSAGES) return;

    int remove_count = (int)messages.size() - MAX_MESSAGES;
    for (int i = 1;i < 1 + remove_count && i < (int)messages.size();i++) {
        pending_messages.push_back(messages[i]);
    }
    messages.erase(messages.begin() + 1, messages.begin() + remove_count + 1);
    loadSystemPrompt();
    
    if ((int)pending_messages.size() < summary_batch) return;
    std::string new_summary = generateSummary(pending_messages, summary);
    if (!new_summary.empty()) summary = new_summary;
    else {
        std::string fallback_summary = summary;
        fallback_summary += "\n[近期对话要点（摘要生成失败，原文片段）]\n";

        int kept = 0;
        for (int i = (int)pending_messages.size() - 1; i >= 0 && kept < 5; --i) {
            const auto& msg = pending_messages[i];
            std::string role = msg["role"];
            std::string content = msg["content"].get<std::string>();
            if (content.size() > 80) content = content.substr(0, 80) + "…";
            fallback_summary += (role == "user" ? "用户: " : display_name + ": ") + content + "\n";
            kept++;
        }

        const size_t MAX_SUMMARY_LEN = 2000; // 若超过2000字，从头部截掉
        if (fallback_summary.size() > MAX_SUMMARY_LEN)
            fallback_summary = fallback_summary.substr(fallback_summary.size() - MAX_SUMMARY_LEN);
        summary = fallback_summary;
    }
    db->setSummary(session_id, user_id, summary);
    pending_messages = json::array();
    db->deleteOldMessages(session_id, user_id, MAX_TURNS);
}

void Conversation::loadFromDatabase() {
    if (!db) return;
    auto history = db->getRecentMessages(session_id, user_id, MAX_TURNS * 2);
    for (const auto& [role, content] : history) {
        messages.push_back({{"role", role}, {"content", content}});
    }
    std::cout << "[系统] 已从数据库加载 " << history.size() << " 条历史消息" << std::endl;
}

void Conversation::add_user_message(const std::string& content) {
    messages.push_back({{"role", "user"}, {"content", content}});
    trim_messages();
    if (db) {
        int turn_id = db->getMaxTurnId(session_id, user_id) + 1;
        db->saveMessage(session_id, user_id, "user", content, turn_id);
        pending_user_msg_id = db->getMaxMsgId(session_id, user_id);
    }
}

void Conversation::flush_user_embeddings(const std::vector<float>& emb) {
    if (db && pending_user_msg_id > 0 && !emb.empty())
        db->saveEmbedding(pending_user_msg_id, session_id, user_id, emb);
    pending_user_msg_id = -1;
}


void Conversation::add_assistant_message(const std::string& content) {
    messages.push_back({{"role", "assistant"}, {"content", content}});
    trim_messages();
    if (db) {
        int turn_id = db->getMaxTurnId(session_id, user_id);
        db->saveMessage(session_id, user_id, "assistant", content, turn_id);

        auto emb = get_embedding(content);
        if (!emb.empty()) {
            int msg_id = db->getMaxMsgId(session_id, user_id);
            db->saveEmbedding(msg_id, session_id, user_id, emb);
        }
    }
}

json Conversation::get_messages() const {
    return messages;
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

std::string Conversation::getSessionId() const {
    return session_id;
}