#pragma once
#include <sqlite3.h>
#include <mutex>
#include <string>
#include <memory>
#include <optional>
#include <vector>
#include "common.h"

namespace ChatSDK
{
    using namespace Common;

    class DataManager
    {
    public:
        DataManager(const std::string& db_name);
        ~DataManager();

        //Session相关操作
        //插入新会话
        bool InsertSession(const Session& session);
        //获取会话
        std::shared_ptr<Session> GetSession(const std::string& session_id);
        //删除会话
        bool DeleteSession(const std::string& session_id);
        //更新会话时间戳
        void UpdateSessionTimestamp(const std::string& session_id,std::time_t timestamp);
        //获取所有会话id
        std::vector<std::string> GetAllSessionIDs() const;
        //获取所有会话信息
        std::vector<std::shared_ptr<Session>> GetAllSessions() const;
        //清空所有会话
        bool ClearAllSession();
        //获取所有会话个数
        size_t GetSessionCount() const;

        //Message相关操作
        //插入新消息
        bool InsertMessage(const std::string& session_id,const Message& message);
        //获取会话消息列表
        std::vector<Message> GetSessionMessages(const std::string& session_id) const;
        //删除指定会话消息
        bool DeleteSessionMessages(const std::string& session_id);

        //ModelConfig相关操作
        //插入或更新模型配置
        bool InsertModelConfig(const ModelConfigRecord& record);
        //删除模型配置
        bool DeleteModelConfig(const std::string& model_name);
        //获取所有模型配置
        std::vector<ModelConfigRecord> GetAllModelConfigs() const;
        //获取指定模型配置
        std::optional<ModelConfigRecord> GetModelConfig(const std::string& model_name) const;
        private:
        //初始化数据库
        bool InitDatabase();
        //执行sql语句
        bool ExecuteSQL(const std::string& sql);
        private:
        sqlite3* db_ = nullptr;
        mutable std::mutex mutex_;
        std::string dbName_;
    };
}
