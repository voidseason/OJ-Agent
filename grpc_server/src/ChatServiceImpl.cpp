#include "../include/ChatServiceImpl.h"
#include "../../ChatSDK_LLM/SDK/util/log.h"
#include <mutex>

namespace OJ_Agent
{

    // 定义一些辅助函数
    namespace{
        //proto转成map<string,string>
        std::map<std::string,std::string> ToParamMap(const ModelConfig& cfg)
        {
            std::map<std::string,std::string> param_map;
            if (!cfg.tag().empty())
              param_map["tag"] = cfg.tag();
            if (!cfg.api_key().empty())
              param_map["api_key"] = cfg.api_key();
            if (!cfg.model().empty())
              param_map["model"] = cfg.model();
            if (!cfg.endpoint().empty())
              param_map["endpoint"] = cfg.endpoint();
            if (!cfg.api_style().empty())
              param_map["api_style"] = cfg.api_style(); 
            if (!cfg.description().empty())
              param_map["description"] = cfg.description();
            if (!cfg.proxy().empty()) {
              param_map["proxy_host"] = cfg.proxy();
              param_map["proxy_port"] = (cfg.proxy_port().empty() ? "7890" : cfg.proxy_port());
            }
            return param_map;
        }
        //提取SendMessageRequest中的所有LLM参数
        std::map<std::string,std::string> BuildRequestParams(const SendMessageRequest& req)
        {
            std::map<std::string, std::string> p;
            if (req.has_temperature())
                p["temperature"] = std::to_string(req.temperature());
            if (req.has_max_tokens())
                p["max_tokens"] = std::to_string(req.max_tokens());
            if (req.has_top_p())
                p["top_p"] = std::to_string(req.top_p());
            if (req.has_top_k())
                p["top_k"] = std::to_string(req.top_k());
            // 用户额外塞进来的 key 优先级最高（覆盖前面的）
            for (const auto &[k, v] : req.extra_params()) {
              p[k] = v;
            }
            return p;
        }
        ModelInfo ToProto(const ChatSDK::Common::ModelInfo& info)
        {
            ModelInfo proto;
            proto.set_model_name(info.modelName_);
            proto.set_description(info.modelDesc_);
            proto.set_provider(info.provider_);
            proto.set_endpoint(info.baseURL_);
            proto.set_is_available(info.isAvailable_);
            return proto;
        }
    }


    // 实现模型管理接口
    ChatServiceImpl::ChatServiceImpl(std::unique_ptr<ChatSDK::Engine> engine)
        : engine_(std::move(engine)) {
      LOG_INFO("ChatServiceImpl constructed");
    }

    grpc::Status ChatServiceImpl::Ping(grpc::ServerContext *context, const Empty *request,
                     Empty *response){
      LOG_INFO("Ping");
      return grpc::Status::OK;
    }

    grpc::Status ChatServiceImpl::AddModel(grpc::ServerContext *context,
                         const AddModelRequest *request,
                         AddModelResponse *response)
    {
        // 1.上锁
        std::lock_guard<std::mutex> lock(engine_mu_);

        // 2.解析参数
        const auto& cfg = request->model_config();
        auto param_map = ToParamMap(cfg);
        LOG_INFO("AddModel: tag={}, model={}", cfg.tag(), cfg.model());

        // 3.添加模型
        bool ok = engine_->AddModel(cfg.tag(), param_map);
        response->set_ok(ok);
        if(!ok){
            response->set_error("AddModel failed");
            LOG_ERR("AddModel failed for tag = {}", cfg.tag());
        }
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::RemoveModel(grpc::ServerContext *context,
                            const RemoveModelRequest *request,
                            Empty *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        bool ok = engine_->RemoveModel(request->model_name());
        if(!ok){
            return grpc::Status(grpc::StatusCode::INTERNAL, "RemoveModel failed");
        }
        LOG_INFO("RemoveModel: model_name={}", request->model_name());
      return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::ListModels(grpc::ServerContext *context, const Empty *request,
                           ListModelsResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        auto models = engine_->GetAvailableModels();
        for(const auto &m :models)
        {
            *response->add_models() = ToProto(m);
        }
        LOG_INFO("ListModels:{} models", models.size());
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::CreateSession(grpc::ServerContext *context,
                            const CreateSessionRequest *request,
                            CreateSessionResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        if(!engine_->IsModelAvailable(request->model_name())){
            return grpc::Status(grpc::StatusCode::NOT_FOUND, "Model not available" + request->model_name());
        }
        auto session_id = engine_->CreateSession(request->model_name());
        if(session_id.empty()) {
          return grpc::Status(grpc::StatusCode::INTERNAL,
                              "CreateSession returned empty id");
        }
        LOG_INFO("CreateSession: model={}, session_id={}", request->model_name(), session_id);
        response->set_session_id(session_id);
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::DeleteSession(grpc::ServerContext *context,
                                   const DeleteSessionRequest *request,
                                   DeleteSessionResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        bool ok = engine_->DeleteSession(request->session_id());
        response->set_ok(ok);
        if(!ok){
            LOG_ERR("DeleteSession: session_id = {}", request->session_id());
            return grpc::Status(grpc::StatusCode::INTERNAL, "DeleteSession failed");
        }
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::ListSessions(grpc::ServerContext *context, const Empty *request,
                 ListSessionsResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        auto ids = engine_->GetAllSessionIds();
        for(const auto &id :ids)
        {
            response->add_session_ids(id);
        }
        LOG_INFO("ListSessions:{} sessions", ids.size());
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::GetMessages(grpc::ServerContext *context, const GetMessagesRequest *request,
                GetMessagesResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        auto messages = engine_->GetSessionMessages(request->session_id());
        for(const auto &msg :messages)
        {
          auto *out = response->add_messages();
          out->set_role(msg.role_);
          out->set_content(msg.content_);
          out->set_timestamp(static_cast<int64_t>(msg.timestamp_));
        }
        LOG_INFO("GetMessages: session_id={}, messages={}",  request->session_id(), messages.size());
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::SendMessage(grpc::ServerContext *context, const SendMessageRequest *request,
                SendMessageResponse *response)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        auto params = BuildRequestParams(*request);
        LOG_INFO("SendMessage: session_id={}, len(content)={}",  request->session_id(), request->message().content().size());

        std::string reply = engine_->SendMessage(request->session_id(), request->message().content());
        if(reply.empty()){
            return grpc::Status(grpc::StatusCode::INTERNAL, "SendMessage returned empty reply");
        }
        response->set_reply(reply);
        return grpc::Status::OK;
    }
    grpc::Status ChatServiceImpl::StreamMessage(grpc::ServerContext *context,
                  const SendMessageRequest *request,
                  grpc::ServerWriter<StreamChunk> *writer)
    {
        std::lock_guard<std::mutex> lock(engine_mu_);
        LOG_INFO("StreamMessage: session_id={}, len(content)={}",  request->session_id(), request->message().content().size());
        std::function<void(const std::string &, bool)> on_chunk =
            [writer, context](const std::string &chunk, bool done) {
              // 客户端已经断开就别再浪费 token 写数据
              if (context->IsCancelled()) {
                return;
              }

              // 构造一块 StreamChunk 准备发出去
              StreamChunk out;
              out.set_chunk(chunk); // 这一小块文本
              out.set_done(done);   // 是否是最后一块

              // 写到 gRPC 流里；如果客户端断了，Write 返回 false
              if (!writer->Write(out)) {
                context->TryCancel(); // 通知 Engine 停止（虽然 Engine 不一定看）
              }
            };

        engine_->SendMessageStream(
            request->session_id(), request->message().content(),on_chunk);
        return grpc::Status::OK;
    }
}

