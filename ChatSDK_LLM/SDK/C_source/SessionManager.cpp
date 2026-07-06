#include "../include/SessionManager.h"
#include "../util/log.h"
#include <ctime>
#include <memory>
#include <algorithm>

namespace ChatSDK
{
    using namespace Common;

    std::atomic<int64_t> SessionManager::message_counter_ = 0;
    std::atomic<int64_t> SessionManager::session_counter_ = 0;

    SessionManager::SessionManager():data_manager_("chat_sdk.db")
    {
        //获取所有会话
        auto sessions = data_manager_.GetAllSessions();
        for(auto& session:sessions) {
            sessions_[session->sessionId_] = session;
        }
    }

    SessionManager::SessionManager(const std::string& db_path):data_manager_(db_path)
    {
        auto sessions = data_manager_.GetAllSessions();
        for(auto& session:sessions) {
            sessions_[session->sessionId_] = session;
        }
    }
    //生成唯一会话id
    std::string SessionManager::GenerateSessionID()
    {
        session_counter_.fetch_add(1);
        std::time_t timestamp = std::time(nullptr);
        return std::to_string(timestamp) + "_" + std::to_string(session_counter_);
    }
    //生成唯一消息id
    std::string SessionManager::GenerateMessageID()
    {
        message_counter_.fetch_add(1);
        std::time_t timestamp = std::time(nullptr);
        return std::to_string(timestamp) + "_msg_" + std::to_string(message_counter_);
    }
    //创建会话
    std::string SessionManager::CreateSession(const std::string& model_name)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //生成会话id
        std::string session_id = GenerateSessionID();
        //创建会话,设置id和模型名称
        auto session = std::make_shared<Session>(model_name);
        session->sessionId_ = session_id;
        sessions_[session_id] = session;
        LOG_INFO("Create session: {}", session_id);
        //将会话写入数据库
        data_manager_.InsertSession(*session);
        return session_id;
    }

    //获取会话
    std::shared_ptr<Session> SessionManager::GetSession(const std::string& session_id)
    {
        //先从内存中查
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = sessions_.find(session_id);
            if (it != sessions_.end()) {
                return it->second;
            }
        }
        //内存中没有，则从数据库中查，顺便给内存备份一份
        //DataManager::GetSession 内部已获取消息列表
        auto session = data_manager_.GetSession(session_id);
        if(session == nullptr) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lock(mutex_);
        //再次检查，因为可能在获取锁中，其他线程已经添加了这个session
        auto it = sessions_.find(session->sessionId_);
        if(it != sessions_.end()) {
            return it->second;
        }
        sessions_[session->sessionId_] = session;
        return session;
    }

    //添加消息
    bool SessionManager::AddMessage(const std::string& session_id, const Message& message)
    {
        std::unique_lock<std::mutex> lock(mutex_);
        auto it = sessions_.find(session_id);
        if(it == sessions_.end()) {
            return false;
        }
        //生成消息id
        Message new_message(message.role_,message.content_);
        new_message.messageId_ = GenerateMessageID();
        new_message.sessionId_ = session_id;
        new_message.timestamp_ = std::time(nullptr);
        //添加消息到会话列表
        it->second->messages_.push_back(new_message);
        it->second->timestamp_ = new_message.timestamp_;
        LOG_INFO("Add message to session: {}", new_message.messageId_);
        lock.unlock();
        //将消息写入数据库
        data_manager_.InsertMessage(session_id,new_message);
        return true;
    }
    //获取会话消息列表
    std::vector<Message> SessionManager::GetSessionMessages(const std::string& session_id)
    {
        //复用GetSession的缓存+DB加载逻辑，避免重复维护
        auto session = GetSession(session_id);
        if(session == nullptr) {
            return {};
        }
        std::lock_guard<std::mutex> lock(mutex_);
        return session->messages_;
    }
    //获取会话列表
    std::vector<std::string> SessionManager::GetSessionList() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        //先从数据库获取所有会话，再合并内存会话（内存时间戳更新，覆盖数据库）
        auto db_sessions = data_manager_.GetAllSessions();
        std::unordered_map<std::string, std::time_t> merged;
        for (const auto& session : db_sessions) {
            merged[session->sessionId_] = session->timestamp_;
        }
        for (const auto& [id, session] : sessions_) {
            merged[id] = session->timestamp_;
        }
        //按时间戳降序排序
        std::vector<std::pair<std::string, std::time_t>> sorted(merged.begin(), merged.end());
        std::sort(sorted.begin(), sorted.end(), [](const auto& a, const auto& b) {
            return a.second > b.second;
        });
        //构造返回值
        std::vector<std::string> session_ids;
        session_ids.reserve(sorted.size());
        for (const auto& [id, _] : sorted) {
            session_ids.push_back(id);
        }
        return session_ids;
    }
    /**
    @brief 删除会话
    @param session_id 会话id
    @return true 成功删除会话, false 会话不存在或删除失败
    */
    bool SessionManager::DeleteSession(const std::string& session_id)
    {
        {
          std::lock_guard<std::mutex> lock(mutex_);
          auto it = sessions_.find(session_id);
          if (it == sessions_.end()) {
            LOG_INFO("Session not found: {}", session_id);
            return false;
          }
          sessions_.erase(it);
          LOG_INFO("Delete session: {}", session_id);
        }
        //删除数据库会话
        data_manager_.DeleteSession(session_id);
        return true;
    }
    /**
    @brief 更新会话时间戳
    @param session_id 会话id
    */
    void SessionManager::UpdateSessionTimestamp(const std::string& session_id)
    {
        std::time_t now = std::time(nullptr);
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = sessions_.find(session_id);
            if (it == sessions_.end()) {
                LOG_ERR("Session not found: {}", session_id);
                return;
            }
            it->second->timestamp_ = now;
            LOG_INFO("Update session timestamp: {}", session_id);
        }
        data_manager_.UpdateSessionTimestamp(session_id, now);
    }
    /**
    @brief 清除所有会话
    */
    void SessionManager::ClearAllSession() {
      {
        std::unique_lock<std::mutex> lock(mutex_);
        sessions_.clear();
      }
      data_manager_.ClearAllSession();
      LOG_INFO("All sessions cleared");
    }

    /**
    @brief 获取会话总数
    @return 会话总数
    */
    size_t SessionManager::GetSessionCount() const
    {
        return data_manager_.GetSessionCount();
    }
}
