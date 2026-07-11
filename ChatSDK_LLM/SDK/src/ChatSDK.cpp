#include "../include/ChatSDK.h"
#include "../include/UnifiedProvider.h"
#include "../util/log.h"

namespace ChatSDK {
    using namespace LLM_Provider;
    using namespace Common;

    // ============================================================
    //  生命周期
    // ============================================================

    Engine::Engine(const std::string& db_path)
        : db_path_(db_path)
    {}

    Engine::~Engine() = default;

    bool Engine::Initialize()
    {
        // 1. 创建 DataManager，指向用户指定的数据库文件
        data_manager_ = std::make_unique<DataManager>(db_path_);

        // 2. 创建 SessionManager，传入同一数据库路径
        session_manager_ = std::make_unique<SessionManager>(db_path_);

        // 3. 创建 LLMManager
        llm_manager_ = std::make_unique<LLMManager>();


        // 4. 设置持久化回调，桥接 LLMManager ↔ DataManager
        llm_manager_->SetPersistenceCallbacks(
            /* save */ [this](const ModelPersistenceData& data) -> bool {
                ModelConfigRecord record;
                record.modelName_     = data.modelName;
                record.providerName_  = data.modelName;   // 每个 model 独立注册 provider
                record.modelDesc_     = data.modelDesc;
                record.baseURL_       = data.baseURL;
                record.apiConfigJson_ = data.paramsJson;
                return data_manager_->InsertModelConfig(record);
            },
            /* load */ [this]() -> std::vector<ModelPersistenceData> {
                auto records = data_manager_->GetAllModelConfigs();
                std::vector<ModelPersistenceData> result;
                result.reserve(records.size());
                for (const auto& r : records) {
                    result.push_back({
                        r.modelName_,
                        r.modelDesc_,
                        r.baseURL_,
                        r.apiConfigJson_
                    });
                }
                return result;
            },
            /* delete */ [this](const std::string& name) -> bool {
                return data_manager_->DeleteModelConfig(name);
            }
        );

        // 5. 注册默认的 UnifiedProvider
        // if (!llm_manager_->RegisterProvider("unified",
        //         std::make_unique<UnifiedProvider>())) {
        //     LOG_ERR("Engine Initialize: failed to register UnifiedProvider");
        //     return false;
        // }

        // 6. 从数据库加载所有已保存的模型（若数据库中有记录，
        //    LoadModelsFromDB 内部会为每条记录再次调用 RegisterProvider + InitModel）
        llm_manager_->LoadModelsFromDB();

        LOG_INFO("Engine Initialize: success (db: {})", db_path_);
        return true;
    }

    // ============================================================
    //  模型管理
    // ============================================================

    bool Engine::AddModel(const std::string& provider_name,
                          const std::map<std::string, std::string>& params)
    {
        // 1. 注册 provider
        if (!llm_manager_->RegisterProvider(provider_name,
                std::make_unique<UnifiedProvider>())) {
            LOG_ERR("Engine AddModel: failed to register provider '{}'", provider_name);
            return false;
        }

        // 2. 初始化模型（InitModel 成功后会自动触发持久化回调，保存到数据库）
        if (!llm_manager_->InitModel(provider_name, params)) {
            LOG_ERR("Engine AddModel: failed to init model '{}'", provider_name);
            return false;
        }

        LOG_INFO("Engine AddModel: model '{}' added", provider_name);
        return true;
    }

    bool Engine::RemoveModel(const std::string& provider_name)
    {
        // DeleteModel 会从内存（providers_ / models_）和数据库中同时移除
        return llm_manager_->DeleteModel(provider_name);
    }

    std::vector<ModelInfo> Engine::GetAvailableModels() const
    {
        return llm_manager_->GetAvailableModels();
    }

    // ============================================================
    //  会话管理
    // ============================================================

    // 创建一个新会话
    std::string Engine::CreateSession(const std::string& model_name)
    {
        if (!llm_manager_->IsModelAvailable(model_name)) {
            LOG_ERR("Engine CreateSession: model '{}' is not available", model_name);
            return "";
        }
        return session_manager_->CreateSession(model_name);
    }

    // 获取指定会话
    std::shared_ptr<Session> Engine::GetSession(const std::string& session_id)
    {
        return session_manager_->GetSession(session_id);
    }

    // 删除指定会话
    bool Engine::DeleteSession(const std::string& session_id)
    {
        return session_manager_->DeleteSession(session_id);
    }

    // 获取所有会话 ID
    std::vector<std::string> Engine::GetAllSessionIds() const
    {
        return session_manager_->GetSessionList();
    }

    // ============================================================
    //  消息操作
    // ============================================================

    // 发送消息
    std::string Engine::SendMessage(const std::string& session_id,
                                    const std::string& content)
    {
        // 1. 获取会话
        auto session = session_manager_->GetSession(session_id);
        if (!session) {
            LOG_ERR("Engine SendMessage: session '{}' not found", session_id);
            return "";
        }

        // 2. 先持久化用户消息（即使 API 调用失败也不丢用户消息）
        Message user_msg("user", content);
        session_manager_->AddMessage(session_id, user_msg);

        // 3. 构建消息上下文（历史消息 + 当前用户消息）
        auto messages = session_manager_->GetSessionMessages(session_id);

        // 4. 调用 LLM 获取回复
        std::map<std::string, std::string> request_params;
        std::string reply = llm_manager_->SendMessage(
            session->modelName_, messages, request_params);

        if (!reply.empty()) {
            // 5. 持久化助理回复
            Message assistant_msg("assistant", reply);
            session_manager_->AddMessage(session_id, assistant_msg);
        }

        return reply;
    }

    void Engine::SendMessageStream(
        const std::string& session_id,
        const std::string& content,
        std::function<void(const std::string&, bool)> callback)
    {
        // 1. 获取会话
        auto session = session_manager_->GetSession(session_id);
        if (!session) {
            LOG_ERR("Engine SendMessageStream: session '{}' not found", session_id);
            if (callback) callback("", true);
            return;
        }

        // 2. 先持久化用户消息
        Message user_msg("user", content);
        session_manager_->AddMessage(session_id, user_msg);

        // 3. 构建消息上下文
        auto messages = session_manager_->GetSessionMessages(session_id);

        // 4. 流式调用 LLM
        //    先收集完整回复用于持久化，同时转发每一块给 UI
        std::string full_reply;
        auto wrapped_callback = [&full_reply, &callback](
            const std::string& chunk, bool done) {
            full_reply += chunk;
            if (callback) callback(chunk, done);
        };

        std::string result = llm_manager_->SendMessageStreamed(
            session->modelName_, messages, {}, wrapped_callback);

        // 如果返回值非空，优先使用返回值（它可能包含完整回复）
        if (!result.empty()) {
            full_reply = result;
        }

        // 5. 持久化助理回复
        if (!full_reply.empty()) {
            Message assistant_msg("assistant", full_reply);
            session_manager_->AddMessage(session_id, assistant_msg);
        }
    }

    std::vector<Message> Engine::GetSessionMessages(
        const std::string& session_id) const
    {
        return session_manager_->GetSessionMessages(session_id);
    }

} // namespace ChatSDK
