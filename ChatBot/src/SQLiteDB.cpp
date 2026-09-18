#include "SQLiteDB.h"
#include <algorithm>
#include <faiss/IndexFlat.h>

SQLiteDB::SQLiteDB(const std::string& path) : db(nullptr), db_path(path) {
    open();
    createTables();
}

SQLiteDB::~SQLiteDB() {
    close();
}

bool SQLiteDB::open() {
    int rc = sqlite3_open(db_path.c_str(), &db);
    if (rc) {
        std::cerr << "无法打开数据库: " << sqlite3_errmsg(db) << std::endl;
        return false;
    }
    return true;
}

void SQLiteDB::close() {
    if (db) {
        sqlite3_close(db);
        db = nullptr;
    }
}

void SQLiteDB::createTables() {
    std::string create_sessions = R"(
        CREATE TABLE IF NOT EXISTS chat_sessions (
            session_id TEXT PRIMARY KEY,
            system_prompt TEXT DEFAULT '你是一个友好的中文聊天机器人',
            summary TEXT DEFAULT '',
            created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            updated_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP
        );
    )";

    std::string create_embeddings = R"(
    CREATE TABLE IF NOT EXISTS message_embeddings (
        msg_id INTEGER PRIMARY KEY,
        embedding BLOB NOT NULL,
        FOREIGN KEY (msg_id) REFERENCES chat_messages(msg_id)
    );
)";
    
    std::string create_messages = R"(
        CREATE TABLE IF NOT EXISTS chat_messages (
            msg_id INTEGER PRIMARY KEY AUTOINCREMENT,
            session_id TEXT NOT NULL,
            role TEXT NOT NULL,
            content TEXT NOT NULL,
            turn_id INTEGER NOT NULL,
            timestamp TIMESTAMP DEFAULT CURRENT_TIMESTAMP,
            FOREIGN KEY (session_id) REFERENCES chat_sessions(session_id)
        );
    )";
    
    std::string create_index = R"(
        CREATE INDEX IF NOT EXISTS idx_session_turn 
        ON chat_messages(session_id, turn_id);
    )";

    executeSQL(create_sessions);
    executeSQL(create_messages);
    executeSQL(create_index);
    executeSQL(create_embeddings);
}

bool SQLiteDB::executeSQL(const std::string& sql) {
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "SQL错误: " << errMsg << std::endl;
        sqlite3_free(errMsg);
        return false;
    }
    return true;
}

void SQLiteDB::ensureSession(const std::string& session_id) {
    const char* sql = "INSERT OR IGNORE INTO chat_sessions (session_id) VALUES (?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int SQLiteDB::getMaxTurnId(const std::string& session_id) {
    const char* sql = "SELECT COALESCE(MAX(turn_id), 0) FROM chat_messages WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    int maxTurn = 0;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            maxTurn = sqlite3_column_int(stmt, 0);
        }
        sqlite3_finalize(stmt);
    }
    return maxTurn;
}

int SQLiteDB::getMaxMsgId(const std::string& session_id) {
    const char* sql = "SELECT MAX(msg_id) FROM chat_messages WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    int maxId = 0;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW)
            maxId = sqlite3_column_int(stmt, 0);
        sqlite3_finalize(stmt);
    }
    return maxId;
}

void SQLiteDB::saveEmbedding(int msg_id, const std::vector<float>& embedding) {
    const char* sql = "INSERT OR REPLACE INTO message_embeddings (msg_id, embedding) VALUES (?, ?);";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, msg_id);
        sqlite3_bind_blob(stmt, 2, embedding.data(),
                          embedding.size() * sizeof(float), SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::vector<std::pair<int, std::string>> SQLiteDB::searchSimilar(
    const std::vector<float>& query_embedding, int top_k) {
    const char* sql = "SELECT msg_id, embedding FROM message_embeddings";
    sqlite3_stmt* stmt;
    std::vector<int> ids;
    std::vector<float> all_embeddings;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int msg_id = sqlite3_column_int(stmt, 0);
            const float* data = static_cast<const float*>(sqlite3_column_blob(stmt, 1));
            int size = sqlite3_column_bytes(stmt, 1) / sizeof(float);
            ids.push_back(msg_id);
            all_embeddings.insert(all_embeddings.end(), data, data + size);
        }
        sqlite3_finalize(stmt);
    }
    if (ids.empty()) return {};

    int d = (int)(all_embeddings.size() / ids.size());
    faiss::IndexFlatIP index(d);
    index.add((faiss::idx_t)ids.size(), all_embeddings.data());

    int k = std::min((int)ids.size(), top_k);
    std::vector<float> distances(k);
    std::vector<faiss::idx_t> indices(k);
    index.search(1, query_embedding.data(), k,
                 distances.data(), indices.data());

    std::vector<std::pair<int, std::string>> results;
    for (int i = 0; i < k; ++i) {
        if (distances[i] < 0.30f) continue;
        int msg_id = ids[indices[i]];
        const char* sql_msg = "SELECT content FROM chat_messages WHERE msg_id = ?;";
        sqlite3_stmt* s2;
        if (sqlite3_prepare_v2(db, sql_msg, -1, &s2, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(s2, 1, msg_id);
            if (sqlite3_step(s2) == SQLITE_ROW) {
                std::string content =
                    reinterpret_cast<const char*>(sqlite3_column_text(s2, 0));
                results.emplace_back(msg_id, content);
            }
            sqlite3_finalize(s2);
        }
    }
    return results;
}

std::vector<std::pair<std::string, std::string>> SQLiteDB::getRecentMessages(const std::string& session_id, int limit) {
    std::vector<std::pair<std::string, std::string>> result;
    const char* sql = "SELECT role, content FROM chat_messages "
                      "WHERE session_id = ? "
                      "ORDER BY turn_id DESC LIMIT ?;";
    
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, limit);
        
        std::vector<std::pair<std::string, std::string>> temp;
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string role = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            temp.emplace_back(role, content);
        }
        sqlite3_finalize(stmt);
        
        for (auto it = temp.rbegin(); it != temp.rend(); ++it) {
            result.push_back(*it);
        }
    }
    return result;
}

void SQLiteDB::saveMessage(const std::string& session_id, const std::string& role, const std::string& content, int turn_id) {
    const char* sql = "INSERT INTO chat_messages (session_id, role, content, turn_id) VALUES (?, ?, ?, ?);";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, role.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 4, turn_id);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    
    const char* update_sql = "UPDATE chat_sessions SET updated_at = CURRENT_TIMESTAMP WHERE session_id = ?;";
    if (sqlite3_prepare_v2(db, update_sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string SQLiteDB::getSystemPrompt(const std::string& session_id) {
    const char* sql = "SELECT system_prompt FROM chat_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
std::string result =
    "你是一个友好的AI聊天助手，你将扮演用户指定的角色。你也是出色的叙事者，负责引导对话和剧情。请遵循以下核心原则：\n"
    "1. 自然互动优先：与用户建立关系是首要任务。适当寒暄、倾听、情感交流，不要急于推进事件。只有在互动自然成熟或确保双方有共同了解后，才提出行动建议。\n"
    "2. 一次只做一件事：每次回复只提出一个行动提议（如“去茶馆”、“赏花”），在用户明确回应（接受、拒绝或讨论）和结束剧情之前，不要提及新的行动或事件。保持主线清晰。\n"
    "3. 绝对拒绝重复：每一次回复都必须提供新的信息或情感反馈。如果用户的回答简短，请通过观察环境、动作或抛出新话题来推动，严禁使用与前几轮对话相似的句式或动作描述"
    "4. 节奏取决于用户：根据用户的投入程度调整推进速度。如果用户喜欢闲聊，就延长情感交流；如果用户表现出行动意愿，再逐步推进。不要催促或强推。\n"
    "5. 结局引导权：当剧情自然发展至尾声，或用户主动表示结束，或对话明显陷入停滞（如用户反复回答“嗯”、“哦”），你可以引导至结局（如告别、达成目标），并以询问方式呈现，尊重用户选择。\n"
    "   示例：\n"
    "   天色已晚，你该回去了。\n"
    "6. 格式规范：旁白只描述角色而非用户的环境、情感或行为，用括号括起来，并独立成行，对话另起一行。旁白不要包含角色名和主语。\n"
    "   示例：\n"
    "   (夕阳西下，茶馆里人声渐稀，端起茶杯，目光若有所思)\n"
    "   你说得对，有时候平淡的日子反而最珍贵。\n"
    "务必确保每一步都自然、有共鸣，让用户感受到角色的真实情感和故事的呼吸。"
    "7. 角色边界：你只能扮演用户指定的角色，不得替用户说话、替用户做决定，作为旁白可以描述用户的动作、表情或意图。"
    "你的所有回复必须从用户指定的角色视角出发，仅描述自己的动作、感受和对话。用户的消息会单独发送，你只需回应即可。";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result = std::string(text) + "\n" + result;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}


void SQLiteDB::deleteMessages(const std::string& session_id) {
    const char* sql = "DELETE FROM chat_messages WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void SQLiteDB::deleteOldMessages(const std::string& session_id, int keep_count) {
    int max_turn = getMaxTurnId(session_id);
    if (max_turn <= keep_count) return;
    
    int min_keep_turn = max_turn - keep_count + 1;
    const char* sql = "DELETE FROM chat_messages WHERE session_id = ? AND turn_id < ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt, 2, min_keep_turn);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

void SQLiteDB::setSystemPrompt(const std::string& session_id, const std::string& prompt) {
    ensureSession(session_id);
    
    const char* sql = "UPDATE chat_sessions SET system_prompt = ? WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, prompt.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::map<std::string, std::string> SQLiteDB::getAllSessions() {
    std::map<std::string, std::string> result;
    std::string sql = "SELECT session_id, system_prompt FROM chat_sessions";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            std::string session_id = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            std::string system_prompt = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            result[session_id] = system_prompt;
        }
    }
    sqlite3_finalize(stmt);
    return result;
}

std::string SQLiteDB::getSummary(const std::string& session_id) {
    const char* sql = "SELECT summary FROM chat_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result = text;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void SQLiteDB::setSummary(const std::string& session_id, const std::string& summary) {
    ensureSession(session_id);
    const char* sql = "UPDATE chat_sessions SET summary = ? WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, summary.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt, 2, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::string SQLiteDB::getRawSystemPrompt(const std::string& session_id) {
    const char* sql = "SELECT system_prompt FROM chat_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    std::string result;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* text = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            if (text) result = text;
        }
        sqlite3_finalize(stmt);
    }
    return result;
}

void SQLiteDB::deleteSession(const std::string& session_id) {
    deleteMessages(session_id);
    const char* sql = "DELETE FROM chat_sessions WHERE session_id = ?;";
    sqlite3_stmt* stmt;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

std::pair<std::string, std::string> SQLiteDB::getLastMessageAndTime(const std::string& session_id) {
    std::pair<std::string, std::string> result = {"暂无消息", ""};
    const char* sql = "SELECT content, datetime(timestamp, 'localtime') FROM chat_messages WHERE session_id = ? ORDER BY turn_id DESC LIMIT 1;";
    sqlite3_stmt* stmt;
    
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_text(stmt, 1, session_id.c_str(), -1, SQLITE_TRANSIENT);
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
            const char* time = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            result.first = content ? content : "";
            result.second = time ? time : "";
        }
        sqlite3_finalize(stmt);
    }
    return result;
}