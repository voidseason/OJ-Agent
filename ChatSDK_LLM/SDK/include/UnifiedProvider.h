#ifndef UNIFIED_PROVIDER_H
#define UNIFIED_PROVIDER_H

#include "LLMProvider.h"
#include <json/json.h>

namespace ChatSDK::LLM_Provider
{

    /**
     * @brief 统一 LLM 提供器
     *
     * 将 DeepSeek / Gemini等（OpenAI 兼容接口）与 OpenAI Responses 接口合并为
     * 一个 Provider。根据 model 名称自动选择正确的 API 风格：
     *
     *   - ChatCompletions 风格（/v1/chat/completions）
     *     → 适用于走 OpenAI 兼容层的模型，例如：deepseek,gemini
     *     → 请求字段：messages / max_tokens
     *     → 响应：choices[0].message.content
     *
     *   - Responses 风格（/v1/responses）
     *     → 适用于 GPT-4 / GPT-3.5 / o1 / o3 等 OpenAI 原生模型
     *     → 请求字段：input / max_output_tokens
     *     → 响应：output[0].content[0].text
     *
     * 使用时通过 LLMManager 注册：
     * @code
     *   auto provider = std::make_unique<UnifiedProvider>();
     *   manager.RegisterProvider("unified", std::move(provider));
     *   manager.InitModel("unified", {
     *       {"model",    "deepseek-chat"},
     *       {"api_key",  "sk-xxx"},
     *       {"endpoint", "https://api.deepseek.com"},
     *       // 可选覆盖 API 风格：{"api_style", "chat_completions"}
     *       // 可选代理：     {"proxy_host", "127.0.0.1"}
     *       //              {"proxy_port", "7890"}
     *   });
     * @endcode
     */
    class UnifiedProvider : public LLMProvider
    {
    public:
        // API 风格
        enum class ApiStyle
        {
            ChatCompletions,    // OpenAI 兼容接口 /v1/chat/completions
            Responses           // OpenAI Responses 接口 /v1/responses
        };

        UnifiedProvider() = default;

        // ---- LLMProvider 接口 ----
        virtual bool Init_Model(
            const std::map<std::string, std::string> &model_config) override;

        virtual bool Is_Available() const override;

        virtual std::string Model_Name() const override;

        virtual std::string Model_Desc() const override;

        virtual std::string Send_Message(
            const std::vector<Common::Message> &messages,
            const std::map<std::string, std::string> &request_params) override;

        virtual std::string Send_Message_Stream(
            const std::vector<Common::Message> &messages,
            const std::map<std::string, std::string> &request_params,
            std::function<void(const std::string &, bool)> callback) override;

    private:
        // ---- 成员 ----
        std::string modelName_;
        std::string modelDesc_;
        ApiStyle apiStyle_ = ApiStyle::ChatCompletions;

        // 代理（可选）
        bool useProxy_ = false;
        std::string proxyHost_;
        int proxyPort_ = 0;

        // ---- 内部工具 ----
        /** @brief 根据模型名自动推测 API 风格 */
        static ApiStyle DetectApiStyle(const std::string &model);

        /** @brief 构建 messages 数组 JSON */
        static Json::Value BuildMessagesJson(
            const std::vector<Common::Message> &messages);

        /** @brief 构建请求体字符串 */
        std::string BuildRequestBody(
            const Json::Value &messages,
            double temperature,
            int maxTokens,
            bool stream) const;

        /** @brief 获取请求路径 */
        std::string GetApiPath() const;

        /** @brief 解析非流式响应正文 → 文本 */
        std::string ParseNonStreamResponse(
            const std::string &body,
            const std::string &providerTag) const;

        /** @brief 处理 ChatCompletions SSE 数据块 */
        bool HandleChatCompletionsSSE(
            const std::string &chunk,
            std::string &sseBuffer,
            std::string &fullResponse,
            bool &completed,
            std::function<void(const std::string &, bool)> &callback);

        /** @brief 处理 Responses SSE 数据块 */
        bool HandleResponsesSSE(
            const std::string &chunk,
            std::string &sseBuffer,
            std::string &fullResponse,
            bool &completed,
            std::function<void(const std::string &, bool)> &callback);
    };

} // namespace ChatSDK::LLM_Provider

#endif // UNIFIED_PROVIDER_H
