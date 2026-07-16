#pragma once
#include <string>
#include <memory>
#include <map>
#include <vector>
#include <functional>
#include "LLMManager.h"
#include "SessionManager.h"
#include "DataManager.h"

/**
 * @brief ChatSDK 顶层入口
 *
 * 封装 LLMManager / SessionManager / DataManager 的初始化和协作逻辑，
 * 为 UI 层提供简洁的接口。使用者只需创建 Engine 实例并调用 Initialize()
 * 即可开始使用。
 *
 * 典型用法：
 * @code
 *   ChatSDK::Engine engine("chat.db");
 *   engine.Initialize();
 *
 *   // 添加一个 DeepSeek 模型
 *   engine.AddModel("deepseek", {
 *       {"api_key",  "sk-xxx"},
 *       {"model",    "deepseek-chat"},
 *       {"endpoint", "https://api.deepseek.com"}
 *   });
 *
 *   // 创建会话并发消息
 *   auto sid = engine.CreateSession("deepseek");
 *   auto reply = engine.SendMessage(sid, "你好");
 * @endcode
 */
namespace ChatSDK {
    using namespace LLM_Provider;
    using namespace Common;

class Engine
{
  public:
    /**
     * @brief 构造函数
     *
     * 仅保存数据库路径，不执行任何初始化操作。真正的初始化请调用 Initialize()。
     *
     * @param db_path SQLite 数据库文件路径，默认值为 "chat.db"
     */
    explicit Engine(const std::string& db_path = "chat.db");

    /**
     * @brief 析构函数
     *
     * 自动清理 DataManager、SessionManager、LLMManager 等子组件。
     */
    ~Engine();

    /**
     * @brief 初始化引擎
     *
     * 完成以下工作：
     *   1. 创建 DataManager 并打开数据库连接
     *   2. 创建 SessionManager
     *   3. 创建 LLMManager
     *   4. 设置持久化回调，将 LLMManager 的保存/加载/删除操作桥接到 DataManager
     *   5. 注册 UnifiedProvider 作为默认的 LLM Provider
     *   6. 从数据库加载所有已保存的模型配置
     *
     * @return true 表示初始化成功，false 表示失败
     */
    bool Initialize();

    // ============================================================
    //  模型管理
    // ============================================================


    /**
     * @brief 检查模型是否可用
     *
     * @param model_name 要检查的模型名称
     * @return true 表示模型可用，false 表示不可用
     */
    bool IsModelAvailable(const std::string& model_name) const;

    /**
     * @brief 添加一个模型
     *
     * 自动完成以下步骤：
     *   1. 注册 UnifiedProvider 到 LLMManager
     *   2. 调用 InitModel 初始化模型配置
     *   3. 将模型配置持久化到 SQLite 数据库
     *
     * 添加成功后，该模型会出现在 GetAvailableModels() 的返回结果中。
     * 下次应用重启时，调用 Initialize() 即可自动恢复。
     *
     * @param tag    模型标签，同时也作为 provider 的注册名。
     *               例如 "deepseek"、"gpt4o"、"gemini" 等。
     *               后续创建会话时传入的 model_name 需与此一致。
     * @param params 模型初始化参数，支持的 key 包括：
     *               - "api_key"    : API 密钥（必需）
     *               - "model"      : 模型名，如 "deepseek-chat"、"gpt-4o"
     *               - "endpoint"   : API 基础 URL（可选，默认 https://api.openai.com）
     *               - "base_url"   : endpoint 的别名（可选）
     *               - "api_style"  : 强制指定 API 风格，"chat_completions" 或 "responses"（可选）
     *               - "description": 模型描述（可选）
     *               - "proxy_host" : 代理主机（可选）
     *               - "proxy_port" : 代理端口（可选，默认 7890）
     * @return true 表示添加成功，false 表示失败
     */
    bool AddModel(const std::string& tag,
                  const std::map<std::string, std::string>& params);

    /**
     * @brief 删除一个模型
     *
     * 从 LLMManager 的内存中和 SQLite 数据库中同时移除该模型的配置。
     * 删除后该模型将不再出现在 GetAvailableModels() 的结果中。
     *
     * @param tag 要删除的模型标签（与 AddModel 时传入的 tag 一致）
     * @return true 表示删除成功，false 表示失败
     */
    bool RemoveModel(const std::string& tag);

    /**
     * @brief 获取所有可用模型列表
     *
     * UI 层调用此方法获取已添加且可用的模型列表，用于填充模型选择下拉框。
     *
     * @return 可用模型的 ModelInfo 列表，每个元素包含模型名称、描述等信息
     */
    std::vector<ModelInfo> GetAvailableModels() const;

    // ============================================================
    //  会话管理
    // ============================================================

    /**
     * @brief 创建一个新会话
     *
     * 创建一个与指定模型绑定的新会话，并持久化到数据库。
     *
     * @param model_name 会话使用的模型名称，必须是已通过 AddModel 添加的 tag
     * @return 会话 ID 字符串，后续操作（如 SendMessage）凭此 ID 引用该会话。
     *         失败返回空字符串。
     */
    std::string CreateSession(const std::string& model_name);

    /**
     * @brief 获取指定会话
     *
     * 根据会话 ID 获取对应的会话对象，包含会话的所有消息历史。
     *
     * @param session_id 会话 ID
     * @return 会话的共享指针，如果会话不存在返回 nullptr
     */
    std::shared_ptr<Session> GetSession(const std::string& session_id);

    /**
     * @brief 删除指定会话
     *
     * 删除会话及其所有消息记录，同时从内存和数据库中移除。
     *
     * @param session_id 要删除的会话 ID
     * @return true 表示删除成功，false 表示失败
     */
    bool DeleteSession(const std::string& session_id);

    /**
     * @brief 获取所有会话 ID 列表
     *
     * UI 层调用此方法获取所有历史会话的 ID，用于展示会话列表。
     *
     * @return 会话 ID 字符串列表
     */
    std::vector<std::string> GetAllSessionIds() const;

    // ============================================================
    //  消息操作
    // ============================================================

    /**
     * @brief 发送消息（全量返回）
     *
     * 流程：
     *   1. 从 SessionManager 获取当前会话的消息历史
     *   2. 将用户消息追加到会话中并持久化
     *   3. 调用 LLMManager 获取 LLM 的完整回复
     *   4. 将助理回复追加到会话中并持久化
     *
     * @param session_id 会话 ID
     * @param content    用户消息的文本内容
     * @return 助理回复的完整文本内容。如果发送失败返回空字符串。
     */
    std::string SendMessage(const std::string& session_id,
                            const std::string& content);

    /**
     * @brief 发送消息（流式返回）
     *
     * 流程同 SendMessage，但 LLM 的回复会通过 callback 分块返回。
     * 每次收到一个文本块时 callback 被调用一次，最后一个块传入 done=true。
     *
     * @param session_id 会话 ID
     * @param content    用户消息的文本内容
     * @param callback   流式回调函数，签名为 void(const std::string& chunk, bool done)
     *                   - chunk: LLM 返回的文本片段
     *                   - done:  是否已完成（最后一个块为 true）
     */
    void SendMessageStream(const std::string& session_id,
                           const std::string& content,
                           std::function<void(const std::string&, bool)> callback);

    /**
     * @brief 获取指定会话的消息历史
     *
     * 返回指定会话中所有的消息，按时间升序排列。
     *
     * @param session_id 会话 ID
     * @return 消息列表，每条消息包含角色（user/assistant）、内容、时间戳等信息
     */
    std::vector<Message> GetSessionMessages(const std::string& session_id) const;

  private:
    std::string                     db_path_;          ///< 数据库文件路径
    std::unique_ptr<DataManager>    data_manager_;     ///< 数据持久化管理器
    std::unique_ptr<SessionManager> session_manager_;  ///< 会话管理器
    std::unique_ptr<LLMManager>     llm_manager_;      ///< LLM 模型管理器
};

} // namespace ChatSDK