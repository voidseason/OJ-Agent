#include "../include/DataManager.h"
#include "../include/common.h"
#include "../util/log.h"

namespace ChatSDK
{
    using namespace Common;
    DataManager::DataManager(const std::string& db_name)
    : dbName_(db_name),db_(nullptr)
    {
        //创建或打开数据库连接
        if(sqlite3_open(db_name.c_str(),&db_)!=SQLITE_OK)
        {
            LOG_ERR("Failed to open database: %s",sqlite3_errmsg(db_));
            return;
        }
        LOG_INFO("Database %s opened successfully",db_name.c_str());

        //初始化数据库表(创建表)
        if(!InitDatabase())
        {
            sqlite3_close(db_);
            db_ = nullptr;
            LOG_ERR("Failed to initialize database tables");
            return;
        }
        LOG_INFO("Database tables initialized successfully");
    }
    DataManager::~DataManager()
    {
        if(db_)
        {
            sqlite3_close(db_);
            db_ = nullptr;
            LOG_INFO("Database %s closed successfully",dbName_.c_str());
        }
    }
    bool DataManager::InitDatabase()
    {
        // 注意：不在此处加锁！ExecuteSQL 内部已持有 mutex_ 锁
        // 启用外键约束
        if(!ExecuteSQL("PRAGMA foreign_keys = ON;")) {
            LOG_WARN("Failed to enable foreign keys");
        }
        //创建Session表
        const std::string create_session_table_sql =
        "CREATE TABLE IF NOT EXISTS Session ("
        "SessionID TEXT PRIMARY KEY NOT NULL,"
        "ModelName TEXT NOT NULL,"
        "Timestamp INTEGER DEFAULT 0);";
        if(!ExecuteSQL(create_session_table_sql))
        {
            LOG_ERR("Failed to create Session table: %s",sqlite3_errmsg(db_));
            return false;
        }

        //创建Message表
        const std::string create_message_table_sql =
        "CREATE TABLE IF NOT EXISTS Message ("
        "MessageID TEXT PRIMARY KEY NOT NULL,"
        "SessionID TEXT NOT NULL,"
        "Role TEXT NOT NULL,"
        "Content TEXT NOT NULL,"
        "Timestamp INTEGER DEFAULT 0,"
        "FOREIGN KEY(SessionID) REFERENCES Session(SessionID) ON DELETE CASCADE"
        ");";
        if(!ExecuteSQL(create_message_table_sql))
        {
            LOG_ERR("Failed to create Message table: %s",sqlite3_errmsg(db_));
            return false;
        }

        //创建索引加速查询
        const std::string create_index_sql =
        "CREATE INDEX IF NOT EXISTS idx_message_sessionid ON Message (SessionID);";
        if(!ExecuteSQL(create_index_sql))
        {
            LOG_ERR("Failed to create index: %s",sqlite3_errmsg(db_));
            return false;
        }
        //创建ModelConfig表
        const std::string create_model_config_table_sql =
        "CREATE TABLE IF NOT EXISTS ModelConfig ("
        "ModelName TEXT PRIMARY KEY NOT NULL,"
        "ProviderName TEXT NOT NULL,"
        "ModelDesc TEXT DEFAULT '',"
        "BaseURL TEXT DEFAULT '',"
        "APIConfig TEXT DEFAULT '{}');";
        if(!ExecuteSQL(create_model_config_table_sql))
        {
            LOG_ERR("Failed to create ModelConfig table: %s",sqlite3_errmsg(db_));
            return false;
        }
        return true;
    }

    bool DataManager::ExecuteSQL(const std::string& sql)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        char* err_msg = nullptr;
        int ret = sqlite3_exec(db_,sql.c_str(),nullptr,nullptr,&err_msg);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to execute SQL: %s",err_msg);
            sqlite3_free(err_msg);
            return false;
        }
        return true;
    }

    bool DataManager::InsertSession(const Session& session)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string insert_sql =
        "INSERT INTO Session (SessionID,ModelName,Timestamp) VALUES (?,?,?);";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,insert_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
        
        //绑定参数
        sqlite3_bind_text(stmt,1,session.sessionId_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,session.modelName_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt,3,static_cast<int64_t>(session.timestamp_));

        //执行语句
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_DONE)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }

        //释放语句
        sqlite3_finalize(stmt);
        return true; 
    }

    std::shared_ptr<Session> DataManager::GetSession(const std::string& session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备sql语句
        const std::string select_sql =
        "SELECT ModelName,Timestamp FROM Session WHERE SessionID = ?;";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return nullptr;
        }
        
        //绑定参数
        sqlite3_bind_text(stmt,1,session_id.c_str(),-1,SQLITE_TRANSIENT);
        
        //执行语句
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_ROW)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return nullptr;
        }
        
        ///创建Session对象
        std::string modelName(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)));
        auto session = std::make_shared<Session>(modelName);
        session->sessionId_ = session_id;
        session->timestamp_ = sqlite3_column_int64(stmt,1);
        
        //释放语句
        sqlite3_finalize(stmt);
        session->messages_ = GetSessionMessages(session_id);
        
        return session;
    }

    void DataManager::UpdateSessionTimestamp(const std::string& session_id,std::time_t timestamp)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备SQL语句
        const std::string update_sql =
        "UPDATE Session SET Timestamp = ? WHERE SessionID = ?;";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,update_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            return;
        }
        
        //绑定参数
        sqlite3_bind_int(stmt,1,static_cast<int64_t>(timestamp));
        sqlite3_bind_text(stmt,2,session_id.c_str(),-1,SQLITE_TRANSIENT);
        
        //执行语句
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_DONE)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return;
        }
        
        //释放语句
        sqlite3_finalize(stmt);
        LOG_INFO("Session timestamp updated: {}", session_id);
    }

    bool DataManager::DeleteSession(const std::string& session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //先删除该会话的所有消息（外键约束，需先删子表）
        {
            const std::string delete_messages_sql =
            "DELETE FROM Message WHERE SessionID = ?;";
            sqlite3_stmt* stmt = nullptr;
            int ret = sqlite3_prepare_v2(db_,delete_messages_sql.c_str(),-1,&stmt,nullptr);
            if(ret!=SQLITE_OK)
            {
                LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
                sqlite3_finalize(stmt);
                return false;
            }
            sqlite3_bind_text(stmt,1,session_id.c_str(),-1,SQLITE_TRANSIENT);
            ret = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if(ret!=SQLITE_DONE)
            {
                LOG_ERR("Failed to delete messages: %s",sqlite3_errmsg(db_));
                return false;
            }
        }
        //再删除会话记录
        {
            const std::string delete_session_sql =
            "DELETE FROM Session WHERE SessionID = ?;";
            sqlite3_stmt* stmt = nullptr;
            int ret = sqlite3_prepare_v2(db_,delete_session_sql.c_str(),-1,&stmt,nullptr);
            if(ret!=SQLITE_OK)
            {
                LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
                sqlite3_finalize(stmt);
                return false;
            }
            sqlite3_bind_text(stmt,1,session_id.c_str(),-1,SQLITE_TRANSIENT);
            ret = sqlite3_step(stmt);
            sqlite3_finalize(stmt);
            if(ret!=SQLITE_DONE)
            {
                LOG_ERR("Failed to delete session: %s",sqlite3_errmsg(db_));
                return false;
            }
        }
        LOG_INFO("Session deleted: {}", session_id);
        return true;
    }

    std::vector<std::string> DataManager::GetAllSessionIDs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备SQL语句
        const std::string select_sql =
        "SELECT SessionID FROM Session;";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return {};
        }
        std::vector<std::string> sessionIds;
        //执行语句
        while(sqlite3_step(stmt)==SQLITE_ROW)
        {
            //获取会话ID
            std::string sessionId(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)));
            sessionIds.push_back(sessionId);
        }
        
        sqlite3_finalize(stmt);
        return sessionIds;
    }
    std::vector<std::shared_ptr<Session>> DataManager::GetAllSessions() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备sql语句
        const std::string select_sql =
        "SELECT SessionID,ModelName,Timestamp FROM Session;";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return {};
        }
        
        std::vector<std::shared_ptr<Session>> sessions;
        //执行语句
        while(sqlite3_step(stmt)==SQLITE_ROW)
        {
            //获取会话ID
            std::string sessionId(reinterpret_cast<const char*>(sqlite3_column_text(stmt,0)));
            std::string modelName(reinterpret_cast<const char*>(sqlite3_column_text(stmt,1)));
            std::time_t timestamp(sqlite3_column_int64(stmt,2));
            auto session = std::make_shared<Session>(modelName);
            session->sessionId_ = sessionId;
            session->timestamp_ = timestamp;
            sessions.push_back(session);
        }
        
        sqlite3_finalize(stmt);
        return sessions;
    }

    size_t DataManager::GetSessionCount() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备sql语句
        const std::string select_sql =
        "SELECT COUNT(*) FROM Session;";
        
        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return 0;
        }
        
        //执行语句
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_ROW)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return 0;
        }
        
        //获取会话数量
        size_t sessionCount = static_cast<size_t>(sqlite3_column_int64(stmt,0));
        sqlite3_finalize(stmt);
        LOG_INFO("Session count: {}", sessionCount);
        return sessionCount;
    }
    bool DataManager::ClearAllSession()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        // 先清空消息表
        {
            const std::string delete_msg_sql = "DELETE FROM Message;";
            char* err_msg = nullptr;
            int ret = sqlite3_exec(db_, delete_msg_sql.c_str(), nullptr, nullptr, &err_msg);
            if (ret != SQLITE_OK) {
                LOG_ERR("Failed to delete all messages: %s", err_msg);
                sqlite3_free(err_msg);
            }
        }
        // 再清空会话表
        {
            const std::string delete_sql = "DELETE FROM Session;";
            char* err_msg = nullptr;
            int ret = sqlite3_exec(db_, delete_sql.c_str(), nullptr, nullptr, &err_msg);
            if (ret != SQLITE_OK) {
                LOG_ERR("Failed to clear sessions: %s", err_msg);
                sqlite3_free(err_msg);
                return false;
            }
        }
        LOG_INFO("All sessions cleared");
        return true;
    }

    bool DataManager::InsertMessage(const std::string& session_id,const Message& message)
    {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          if (session_id != message.sessionId_) {
            LOG_ERR("Session ID not match: {}, {}", session_id,
                    message.sessionId_);
            return false;
          }
          // 准备SQL语句
          const std::string insert_sql = "INSERT INTO Message "
                                         "(MessageID,SessionID,Role,Content,"
                                         "Timestamp) VALUES (?,?,?,?,?);";

          sqlite3_stmt *stmt = nullptr;
          int ret =
              sqlite3_prepare_v2(db_, insert_sql.c_str(), -1, &stmt, nullptr);
          if (ret != SQLITE_OK) {
            LOG_ERR("Failed to prepare statement: %s", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
          }

          // 绑定参数
          sqlite3_bind_text(stmt, 1, message.messageId_.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 2, message.sessionId_.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 3, message.role_.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_text(stmt, 4, message.content_.c_str(), -1,
                            SQLITE_TRANSIENT);
          sqlite3_bind_int64(stmt, 5, static_cast<int64_t>(message.timestamp_));

          // 执行语句
          ret = sqlite3_step(stmt);
          if (ret != SQLITE_DONE) {
            LOG_ERR("Failed to execute statement: %s", sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
          }

          // 释放语句
          sqlite3_finalize(stmt);
          LOG_INFO("Message inserted: {}", message.messageId_);
        }
        //同时更新session的时间戳
        UpdateSessionTimestamp(session_id,message.timestamp_);
        return true;
    }
    std::vector<Message> DataManager::GetSessionMessages(const std::string& session_id) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备sql语句
        const std::string select_sql =
        "SELECT MessageID,Role,Content,Timestamp FROM Message WHERE SessionID = ? ORDER BY Timestamp ASC;";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return {};
        }
        
        //绑定参数
        sqlite3_bind_text(stmt,1,session_id.c_str(),-1,SQLITE_TRANSIENT);
        
        //执行语句
        //获取消息列表
        std::vector<Message> messages;
        while((ret=sqlite3_step(stmt))==SQLITE_ROW)
        {
            std::string role = reinterpret_cast<const char*>(sqlite3_column_text(stmt,1));
            std::string content = reinterpret_cast<const char*>(sqlite3_column_text(stmt,2));
            Message message(role,content);
            message.messageId_ = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
            message.timestamp_ = static_cast<std::time_t>(sqlite3_column_int64(stmt,3));
            messages.push_back(message);
        }
        sqlite3_finalize(stmt);
        return messages;
    }

    bool DataManager::DeleteSessionMessages(const std::string &session_id)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //准备SQL语句
        const std::string delete_sql =
        "DELETE FROM Message WHERE SessionID = ?;";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,delete_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
        
        //绑定参数
        sqlite3_bind_text(stmt,1,session_id.c_str(),-1,SQLITE_TRANSIENT);
        
        //执行语句
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_DONE)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
        
        //释放语句
        sqlite3_finalize(stmt);
        LOG_INFO("Session messages deleted: {}", session_id);
        return true;
    }

    // =============================================================
    //  ModelConfig CRUD
    // =============================================================
    bool DataManager::InsertModelConfig(const ModelConfigRecord& record)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string insert_sql =
            "INSERT OR REPLACE INTO ModelConfig "
            "(ModelName,ProviderName,ModelDesc,BaseURL,APIConfig) "
            "VALUES (?,?,?,?,?);";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,insert_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_bind_text(stmt,1,record.modelName_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,2,record.providerName_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,3,record.modelDesc_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,4,record.baseURL_.c_str(),-1,SQLITE_TRANSIENT);
        sqlite3_bind_text(stmt,5,record.apiConfigJson_.c_str(),-1,SQLITE_TRANSIENT);

        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_DONE)
        {
            LOG_ERR("Failed to execute statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }
        sqlite3_finalize(stmt);
        LOG_INFO("ModelConfig saved: {}", record.modelName_);
        return true;
    }

    bool DataManager::DeleteModelConfig(const std::string& model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string delete_sql =
            "DELETE FROM ModelConfig WHERE ModelName = ?;";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,delete_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return false;
        }

        sqlite3_bind_text(stmt,1,model_name.c_str(),-1,SQLITE_TRANSIENT);
        ret = sqlite3_step(stmt);
        sqlite3_finalize(stmt);

        if(ret!=SQLITE_DONE)
        {
            LOG_ERR("Failed to delete ModelConfig: %s",sqlite3_errmsg(db_));
            return false;
        }
        LOG_INFO("ModelConfig deleted: {}", model_name);
        return true;
    }

    std::vector<ModelConfigRecord> DataManager::GetAllModelConfigs() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string select_sql =
            "SELECT ModelName,ProviderName,ModelDesc,BaseURL,APIConfig "
            "FROM ModelConfig;";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return {};
        }

        std::vector<ModelConfigRecord> records;
        while(sqlite3_step(stmt)==SQLITE_ROW)
        {
            ModelConfigRecord rec;
            rec.modelName_     = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
            rec.providerName_  = reinterpret_cast<const char*>(sqlite3_column_text(stmt,1));
            rec.modelDesc_     = reinterpret_cast<const char*>(sqlite3_column_text(stmt,2));
            rec.baseURL_       = reinterpret_cast<const char*>(sqlite3_column_text(stmt,3));
            rec.apiConfigJson_ = reinterpret_cast<const char*>(sqlite3_column_text(stmt,4));
            records.push_back(std::move(rec));
        }
        sqlite3_finalize(stmt);
        return records;
    }

    std::optional<ModelConfigRecord> DataManager::GetModelConfig(const std::string& model_name) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string select_sql =
            "SELECT ModelName,ProviderName,ModelDesc,BaseURL,APIConfig "
            "FROM ModelConfig WHERE ModelName = ?;";

        sqlite3_stmt* stmt = nullptr;
        int ret = sqlite3_prepare_v2(db_,select_sql.c_str(),-1,&stmt,nullptr);
        if(ret!=SQLITE_OK)
        {
            LOG_ERR("Failed to prepare statement: %s",sqlite3_errmsg(db_));
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        sqlite3_bind_text(stmt,1,model_name.c_str(),-1,SQLITE_TRANSIENT);
        ret = sqlite3_step(stmt);
        if(ret!=SQLITE_ROW)
        {
            sqlite3_finalize(stmt);
            return std::nullopt;
        }

        ModelConfigRecord rec;
        rec.modelName_     = reinterpret_cast<const char*>(sqlite3_column_text(stmt,0));
        rec.providerName_  = reinterpret_cast<const char*>(sqlite3_column_text(stmt,1));
        rec.modelDesc_     = reinterpret_cast<const char*>(sqlite3_column_text(stmt,2));
        rec.baseURL_       = reinterpret_cast<const char*>(sqlite3_column_text(stmt,3));
        rec.apiConfigJson_ = reinterpret_cast<const char*>(sqlite3_column_text(stmt,4));
        sqlite3_finalize(stmt);
        return rec;
    }
}