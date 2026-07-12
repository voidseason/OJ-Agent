#pragma once
#include <map>   
#include <memory> 
#include <mutex> 
#include <string> 
#include <vector> 

#include <grpcpp/grpcpp.h> // gRPC

#include "../../ChatSDK_LLM/SDK/include/ChatSDK.h"  
#include "../build/ChatService.grpc.pb.h" // protoc 生成的接口（注意：不是 .pb.h 是 .grpc.pb.h）

namespace OJ_Agent {

// 这个类实现 ChatService 服务，挂到 gRPC server 上。
class ChatServiceImpl final : public ChatService::Service {
public:
  // 构造函数：注入 Engine 实例
  explicit ChatServiceImpl(std::unique_ptr<ChatSDK::Engine> engine);

  // 析构函数：默认就够了，因为有 unique_ptr 会自动 delete
  ~ChatServiceImpl() override = default;

  // 禁止拷贝和赋值
  // 原因：内部有 std::mutex 和 unique_ptr<Engine>
  ChatServiceImpl(const ChatServiceImpl &) = delete;
  ChatServiceImpl &operator=(const ChatServiceImpl &) = delete;
  // 移动也可以禁掉（unique_ptr 本身就可以 move）
  ChatServiceImpl(ChatServiceImpl &&) = delete;
  ChatServiceImpl &operator=(ChatServiceImpl &&) = delete;

  // ===================================================
  //   RPC 方法
  // ===================================================

  // Ping 方法：用于测试连接是否正常
  grpc::Status Ping(grpc::ServerContext *context, const Empty *request,
                    Empty *response) override;

  // ---------- 模型管理 ----------
  // 添加模型（客户端 → 服务端调用）
  // request 里是完整的 ModelConfig
  // response 里返回 ok/error
  grpc::Status AddModel(grpc::ServerContext *context,
                        const AddModelRequest *request,
                        AddModelResponse *response) override;

  // 删除模型（按 tag）
  // request 是 RemoveModelRequest（只含 tag 字段）
  // response 是 Empty（成功就行，不需要回东西）
  grpc::Status RemoveModel(grpc::ServerContext *context,
                           const RemoveModelRequest *request,
                           Empty *response) override;

  // 列出所有已注册模型
  // request 是 Empty
  // response 里包含 ListModelsResponse（repeated ModelInfo）
  grpc::Status ListModels(grpc::ServerContext *context, const Empty *request,
                          ListModelsResponse *response) override;

  // ---------- 会话管理 ----------
  // 创建会话（按 model_name）
  // request 里是 CreateSessionRequest（含 model_name, system_prompt,
  // default_params） response 里返回新生成的 session_id
  grpc::Status CreateSession(grpc::ServerContext *context,
                             const CreateSessionRequest *request,
                             CreateSessionResponse *response) override;

  // 删除会话（按 session_id）
  grpc::Status DeleteSession(grpc::ServerContext *context,
                             const DeleteSessionRequest *request,
                             DeleteSessionResponse *response) override;

  // 列出所有会话 ID
  grpc::Status ListSessions(grpc::ServerContext *context, const Empty *request,
                            ListSessionsResponse *response) override;

  // 获取某个会话的所有消息历史
  grpc::Status GetMessages(grpc::ServerContext *context,
                           const GetMessagesRequest *request,
                           GetMessagesResponse *response) override;

  // ---------- 消息操作 ----------
  // 非流式发消息
  // request 里是 SendMessageRequest（含 session_id, content, temperature 等）
  // response 里是 SendMessageResponse（含完整 reply）
  grpc::Status SendMessage(grpc::ServerContext *context,
                           const SendMessageRequest *request,
                           SendMessageResponse *response) override;

  // 流式发消息
  // 跟 SendMessage 一样，但是用 ServerWriter 一个 chunk 一个 chunk 发出去
  // 返回值仍然是 Status，最后一个 chunk 之前 done=false，最后一个 done=true
  //
  // 关键参数类型不一样了：
  //   普通方法：response 是 SendMessageResponse*  （一个对象）
  //   流式方法：writer 是 ServerWriter<StreamChunk>*  （多次调 writer->Write()
  //   发多个）
  grpc::Status StreamMessage(grpc::ServerContext *context,
                             const SendMessageRequest *request,
                             grpc::ServerWriter<StreamChunk> *writer) override;

private:
  // Egine可以进行模型管理（包括模型添加会话等等，总之打包了）
  std::unique_ptr<ChatSDK::Engine> engine_;

  // 保护 engine_ 的互斥锁
  // 原因：gRPC 可能多个线程同时调你这 10 个方法，Engine 不是线程安全的
  // 每个方法入口必须先 lock_guard<std::mutex> lk(engine_mu_);
  std::mutex engine_mu_;
};

} // namespac OJ-Agent