#pragma once
#include "common.h"
#include <memory>

class SQLiteDB;

class Conversation {
private:
    json messages;
    int max_client_messages = 15;
    std::string session_id;
    std::string display_name;
    std::string summary;
    std::shared_ptr<SQLiteDB> db;
    bool loaded = false;
    
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
    void add_assistant_message(const std::string& content);
    json get_messages() const;
    std::string getSessionId() const;
};