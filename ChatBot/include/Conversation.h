#pragma once
#include "common.h"
#include <memory>

class SQLiteDB;

class Conversation {
private:
    static constexpr int MAX_TURNS = 15;
    static constexpr int MAX_MESSAGES = MAX_TURNS * 2 + 1;

    json messages;
    json pending_messages = json::array();
    int summary_batch = 8;
    std::string session_id;
    std::string user_id;
    std::string display_name;
    std::string summary;
    std::shared_ptr<SQLiteDB> db;
    int pending_user_msg_id = -1;
    void loadSystemPrompt();
    void trim_messages();
    void loadFromDatabase();
    std::string generateSummary(const json& old_messages, const std::string& old_summary);
    
public:
    Conversation();
    std::shared_ptr<SQLiteDB> getDB() const { return db; }
    void setDisplayName(const std::string& name);
    void initDB(std::shared_ptr<SQLiteDB> database, const std::string& sid);
    void add_user_message(const std::string& content);
    void flush_user_embeddings(const std::vector<float>& emb);
    void add_assistant_message(const std::string& content);
    void setUserId(const std::string& uid) { user_id = uid; }
    std::string getUserId() const { return user_id; }
    json get_messages() const;
    std::string getSessionId() const;
};