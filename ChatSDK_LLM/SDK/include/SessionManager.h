#pragma once
#include <vector>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include "common.h"
#include "DataManager.h"

namespace ChatSDK
{
    using namespace Common;
    class SessionManager
    {
    public:

        SessionManager();
        
        //使用指定数据库路径创建会话管理器
        explicit SessionManager(const std::string& db_path);

        //创建会话 model_name:模型名称 返回会话id
        std::string CreateSession(const std::string& model_name);

        //获取会话
        std::shared_ptr<Session> GetSession(const std::string& session_id);

        //删除会话
        bool DeleteSession(const std::string& session_id);

        //添加消息到会话
        bool AddMessage(const std::string& session_id,const Message& message);

        //获取会话列表
        std::vector<std::string> GetSessionList() const;

        //更新会话时间戳
        void UpdateSessionTimestamp(const std::string& session_id);

        //获取会话消息列表
        std::vector<Message> GetSessionMessages(const std::string& session_id);

        //清空所有会话
        void ClearAllSession();

        //获取会话总数
        size_t GetSessionCount() const;

        private:
        //生成唯一会话id
        std::string GenerateSessionID();
        //生成唯一消息id
        std::string GenerateMessageID();

        private:
        std::unordered_map<std::string,std::shared_ptr<Session>> sessions_;
        mutable std::mutex mutex_;
        static std::atomic<int64_t> message_counter_;
        static std::atomic<int64_t> session_counter_;
        DataManager data_manager_; // 数据管理器
    };
}
