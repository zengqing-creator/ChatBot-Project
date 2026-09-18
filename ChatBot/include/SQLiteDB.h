#pragma once
#include "common.h"

class SQLiteDB {
private:
    sqlite3* db;
    std::string db_path;
    
    static int callback(void* NotUsed, int argc, char** argv, char** azColName);
    
public:
    SQLiteDB(const std::string& path = "chatbot.db");
    ~SQLiteDB();
    
    bool open();
    void close();
    void createTables();
    bool executeSQL(const std::string& sql);
    void ensureSession(const std::string& session_id);
    int getMaxTurnId(const std::string& session_id);
    std::vector<std::pair<std::string, std::string>> getRecentMessages(const std::string& session_id, int limit);
    void saveMessage(const std::string& session_id, const std::string& role, const std::string& content, int turn_id);
    std::string getSystemPrompt(const std::string& session_id);
    void deleteMessages(const std::string& session_id);
    void deleteOldMessages(const std::string& session_id, int keep_count);
    void setSystemPrompt(const std::string& session_id, const std::string& prompt);
    std::map<std::string, std::string> getAllSessions();
    std::string getSummary(const std::string& session_id);
    void setSummary(const std::string& session_id, const std::string& summary);
    std::string getRawSystemPrompt(const std::string& session_id);
    void deleteSession(const std::string& session_id);
    std::pair<std::string, std::string> getLastMessageAndTime(const std::string& session_id);
};