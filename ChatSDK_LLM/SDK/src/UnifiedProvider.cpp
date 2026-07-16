#include "../include/UnifiedProvider.h"
#include "../util/log.h"
#include "httplib.h"
#include <json/json.h>
#include <json/reader.h>
#include <json/value.h>
#include <sstream>

using namespace ChatSDK::LLM_Provider;

// ============================================================
//  模型传入参数的一些默认常量
// ============================================================
#define UNIFIED_DEFAULT_TEMPERATURE 0.7
#define UNIFIED_DEFAULT_MAX_TOKENS 2048
#define UNIFIED_DEFAULT_MAX_OUTPUT 5000

// ============================================================
//  DetectApiStyle — 根据模型名自动判断 API 风格
// ============================================================
UnifiedProvider::ApiStyle UnifiedProvider::DetectApiStyle(
    const std::string &model)
{
    // 转换成小写便于匹配
    std::string m;
    m.reserve(model.size());
    for (auto c : model)
        m.push_back(static_cast<char>(std::tolower(c)));

    // OpenAI 原生模型名 → Responses API
    if (m.compare(0, 4, "gpt-") == 0 ||
        m.compare(0, 2, "o1")  == 0 ||
        m == "o3-mini")
    {
        return ApiStyle::Responses;
    }

    return ApiStyle::ChatCompletions;
}

// ============================================================
//  Init_Model：根据model_config初始化模型
// ============================================================
bool UnifiedProvider::Init_Model(
    const std::map<std::string, std::string> &model_config)
{
    // ---- api_key ----
    auto it = model_config.find("api_key");
    if (it == model_config.end() || it->second.empty()) {
        LOG_ERR("UnifiedProvider Init_Model: api_key not found or empty");
        return false;
    }
    apiKey_ = it->second;

    // ---- endpoint（兼容 base_url 写法） ----
    it = model_config.find("endpoint");
    if (it == model_config.end()) {
        it = model_config.find("base_url");
    }
    if (it != model_config.end()) {
        baseURL_ = it->second;
    } else {
        baseURL_ = "https://api.openai.com";
    }

    // ---- model name ----
    it = model_config.find("model");
    if (it != model_config.end()) {
        modelName_ = it->second;
    } else {
        modelName_ = "gpt-3.5-turbo";
    }

    // ---- description ----
    it = model_config.find("description");
    modelDesc_ = (it != model_config.end())
                     ? it->second
                     : "Unified Provider (" + modelName_ + ")";

    // ---- API 风格：显式指定优先，否则自动检测 ----
    it = model_config.find("api_style");
    if (it != model_config.end()) {
        if (it->second == "responses")
            apiStyle_ = ApiStyle::Responses;
        else
            apiStyle_ = ApiStyle::ChatCompletions;
    } else {
        apiStyle_ = DetectApiStyle(modelName_);
    }

    // ---- 代理（可选） ----
    it = model_config.find("proxy_host");
    if (it != model_config.end()) {
        useProxy_ = true;
        proxyHost_ = it->second;
        it = model_config.find("proxy_port");
        proxyPort_ = (it != model_config.end()) ? std::stoi(it->second) : 7890;
    } else {
        useProxy_ = false;
    }

    isAvailable_ = true;
    LOG_INFO("UnifiedProvider init success: model={}, endpoint={}, style={}",
             modelName_, baseURL_,
             (apiStyle_ == ApiStyle::Responses) ? "Responses" : "ChatCompletions");
    return true;
}

// ============================================================
//  Is_Available / Model_Name / Model_Desc：获取模型信息
// ============================================================
bool UnifiedProvider::Is_Available() const { return isAvailable_; }

std::string UnifiedProvider::Model_Name() const { return modelName_; }

std::string UnifiedProvider::Model_Desc() const { return modelDesc_; }

// ============================================================
//  BuildMessagesJson : 将 Message 列表转换成 JSON 数组
// ============================================================
Json::Value UnifiedProvider::BuildMessagesJson(
    const std::vector<Common::Message> &messages)
{
    LOG_TRACE("BuildMessagesJson: building {} messages", messages.size());
    Json::Value arr(Json::arrayValue);
    for (const auto &msg : messages) {
        Json::Value item;
        item["role"]    = msg.role_;
        item["content"] = msg.content_;
        arr.append(std::move(item));
    }
    return arr;
}

// ============================================================
//  BuildRequestBody — 根据 API 风格构建请求 JSON
// ============================================================
std::string UnifiedProvider::BuildRequestBody(
    const Json::Value &messages,
    double temperature,
    int maxTokens,
    bool stream) const
{
    Json::Value body;
    body["model"] = modelName_;

    if (apiStyle_ == ApiStyle::Responses) {
        body["input"]             = messages;
        body["temperature"]       = temperature;
        body["max_output_tokens"] = maxTokens;
        body["stream"]            = stream;
    } else {
        body["messages"]    = messages;
        body["temperature"] = temperature;
        body["max_tokens"]  = maxTokens;
        body["stream"]      = stream;
    }

    LOG_TRACE("BuildRequestBody: apiStyle={}",
              (apiStyle_ == ApiStyle::Responses) ? "Responses" : "ChatCompletions");
    Json::StreamWriterBuilder writer;
    return Json::writeString(writer, body);
}

// ============================================================
//  GetApiPath — 根据 API 风格返回请求路径
// ============================================================
std::string UnifiedProvider::GetApiPath() const
{
    return (apiStyle_ == ApiStyle::Responses)
               ? "/v1/responses"
               : "/v1/chat/completions";
}

// ============================================================
//  ParseNonStreamResponse — 解析非流式 JSON 响应
// ============================================================
std::string UnifiedProvider::ParseNonStreamResponse(
    const std::string &body,
    const std::string &providerTag) const
{
    Json::Reader reader;
    Json::Value root;
    if (!reader.parse(body, root)) {
        LOG_ERR("UnifiedProvider {}: parse response body failed", providerTag);
        return "Invalid response format from API";
    }

    if (apiStyle_ == ApiStyle::Responses)
    {
        // Responses: output[0].content[0].text
        if (root.isMember("output") && root["output"].isArray() &&
            !root["output"].empty())
        {
            const auto &first = root["output"][0];
            if (first.isMember("content") && first["content"].isArray() &&
                !first["content"].empty() &&
                first["content"][0].isMember("text"))
            {
                std::string result = first["content"][0]["text"].asString();
                LOG_DEBUG("UnifiedProvider {}: Responses content extracted, len={}",
                          providerTag, result.size());
                return result;
            }
        }
        LOG_ERR("UnifiedProvider {}: output content not found", providerTag);
        return "Invalid response format from Responses API";
    }

    // ChatCompletions: choices[0].message.content
    if (root.isMember("choices") && root["choices"].isArray() &&
        !root["choices"].empty())
    {
        const auto &first = root["choices"][0];
        if (first.isMember("message") && first["message"].isMember("content")) {
            std::string result = first["message"]["content"].asString();
            LOG_DEBUG("UnifiedProvider {}: ChatCompletions content extracted, len={}",
                      providerTag, result.size());
            return result;
        }
        LOG_ERR("UnifiedProvider {}: message content not found", providerTag);
        return "Invalid response format from ChatCompletions API";
    }

    LOG_ERR("UnifiedProvider {}: choices/output not found in response",
            providerTag);
    return "Invalid response format from API";
}

// ============================================================
//  Send_Message（非流式）
// ============================================================
std::string UnifiedProvider::Send_Message(
    const std::vector<Common::Message> &messages,
    const std::map<std::string, std::string> &request_params)
{
    const std::string tag = "Send_Message";

    if (!Is_Available()) {
        LOG_ERR("UnifiedProvider {}: model is not init", tag);
        return "";
    }

    // ---- 参数解析 ----
    double temperature = UNIFIED_DEFAULT_TEMPERATURE;
    int maxTokens = (apiStyle_ == ApiStyle::Responses)
                        ? UNIFIED_DEFAULT_MAX_OUTPUT
                        : UNIFIED_DEFAULT_MAX_TOKENS;

    auto it = request_params.find("temperature");
    if (it != request_params.end())
        temperature = std::stod(it->second);

    // max_tokens 和 max_output_tokens 都兼容
    it = request_params.find("max_tokens");
    if (it != request_params.end())
        maxTokens = std::stoi(it->second);

    it = request_params.find("max_output_tokens");
    if (it != request_params.end())
        maxTokens = std::stoi(it->second);

    LOG_INFO("UnifiedProvider {}: model={}, temperature={}, maxTokens={}, msgCount={}",
             tag, modelName_, temperature, maxTokens, messages.size());

    // ---- 构建请求 ----
    Json::Value messagesJson = BuildMessagesJson(messages);
    std::string requestBody = BuildRequestBody(messagesJson, temperature,
                                               maxTokens, false);
    LOG_TRACE("UnifiedProvider {}: request body: {}", tag, requestBody);

    // ---- HTTP 客户端 ----
    LOG_DEBUG("UnifiedProvider {}: POST {} to {}", tag, GetApiPath(), baseURL_);
    httplib::Client client(baseURL_.c_str());
    client.set_connection_timeout(30, 0);   // 30 秒超时
    client.set_read_timeout(60, 0);         // 60 秒读取超时
    if (useProxy_)
        client.set_proxy(proxyHost_.c_str(), proxyPort_);

    // ---- 请求头 ----
    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer " + apiKey_);
    headers.emplace("Content-Type", "application/json");

    // ---- 发送请求 ----
    auto res = client.Post(GetApiPath().c_str(), headers, requestBody,
                           "application/json");
    if (!res) {
        LOG_ERR("UnifiedProvider {}: request failed (connection) to {}",
                tag, baseURL_);
        return "";
    }

    LOG_DEBUG("UnifiedProvider {}: status code: {}", tag, res->status);
    if (res->status != 200) {
        LOG_ERR("UnifiedProvider {}: request failed, status: {} - {}",
                tag, res->status, res->body);
        return "";
    }

    LOG_DEBUG("UnifiedProvider {}: response: {}", tag, res->body);
    return ParseNonStreamResponse(res->body, tag);
}

// ============================================================
//  HandleChatCompletionsSSE — 解析 ChatCompletions SSE 流
// ============================================================
bool UnifiedProvider::HandleChatCompletionsSSE(
    const std::string &chunk,
    std::string &sseBuffer,
    std::string &fullResponse,
    bool &completed,
    std::function<void(const std::string &, bool)> &callback)
{
    sseBuffer.append(chunk);

    size_t pos = 0;
    while (true) {
        // 查找事件分隔符
        size_t delim_pos = sseBuffer.find("\n\n", pos);
        if (delim_pos == std::string::npos)
            break;

        std::string event = sseBuffer.substr(pos, delim_pos - pos);
        pos = delim_pos + 2;

        // 跳过空事件和注释事件
        if (event.empty() || event[0] == ':')
            continue;

        if (event.compare(0, 6, "data: ") == 0) {
            std::string payload = event.substr(6);

            if (payload == "[DONE]") {
                LOG_DEBUG("UnifiedProvider HandleChatCompletionsSSE: "
                          "stream done, fullResponse len={}", fullResponse.size());
                callback("", true);
                completed = true;
                break;
            }

            Json::Reader reader;
            Json::Value json;
            if (!reader.parse(payload, json)) {
                LOG_ERR("UnifiedProvider HandleChatCompletionsSSE: "
                        "parse SSE data failed: {}", payload);
                continue;
            }

            if (json.isMember("choices") && json["choices"].isArray() &&
                !json["choices"].empty())
            {
                const auto &choice = json["choices"][0];
                if (choice.isMember("delta") &&
                    choice["delta"].isMember("content"))
                {
                    std::string content = choice["delta"]["content"].asString();
                    if (!content.empty()) {
                        callback(content, false);
                        fullResponse += content;
                    }
                }
            }
        }
    }

    sseBuffer.erase(0, pos);
    return true;
}

// ============================================================
//  HandleResponsesSSE — 解析 Responses API SSE 流
// ============================================================
bool UnifiedProvider::HandleResponsesSSE(
    const std::string &chunk,
    std::string &sseBuffer,
    std::string &fullResponse,
    bool &completed,
    std::function<void(const std::string &, bool)> &callback)
{
    sseBuffer.append(chunk);

    while (true) {
        size_t delim_pos = sseBuffer.find("\n\n");
        if (delim_pos == std::string::npos)
            break;

        std::string block = sseBuffer.substr(0, delim_pos);
        sseBuffer.erase(0, delim_pos + 2);

        if (block.empty() || block[0] == ':')
            continue;

        // 逐行解析 event: / data:
        std::istringstream stream(block);
        std::string line, eventType, dataStr;
        while (std::getline(stream, line)) {
            if (line.compare(0, 6, "event:") == 0) {
                eventType = line.substr(7);  // 跳过空格
            } else if (line.compare(0, 5, "data:") == 0) {
                dataStr = line.substr(5);
                if (!dataStr.empty() && dataStr[0] == ' ')
                    dataStr = dataStr.substr(1);
            }
        }

        if (dataStr.empty())
            continue;

        Json::Reader reader;
        Json::Value json;
        if (!reader.parse(dataStr, json)) {
            LOG_ERR("UnifiedProvider HandleResponsesSSE: "
                    "parse SSE data failed: {}", dataStr);
            continue;
        }

        if (eventType == "response.output_text.delta") {
            if (json.isMember("delta") && json["delta"].isString()) {
                std::string delta = json["delta"].asString();
                if (!delta.empty())
                    callback(delta, false);
            }
        } else if (eventType == "response.output_item.done") {
            if (json.isMember("item") && json["item"].isObject()) {
                const auto &item = json["item"];
                if (item.isMember("content") && item["content"].isArray() &&
                    !item["content"].empty() &&
                    item["content"][0].isMember("text") &&
                    item["content"][0]["text"].isString())
                {
                    fullResponse += item["content"][0]["text"].asString();
                }
            }
        } else if (eventType == "response.completed") {
            LOG_DEBUG("UnifiedProvider HandleResponsesSSE: "
                      "stream completed, fullResponse len={}", fullResponse.size());
            callback("", true);
            completed = true;
            return true;
        }
    }

    return true;
}

// ============================================================
//  Send_Message_Stream（流式）
// ============================================================
std::string UnifiedProvider::Send_Message_Stream(
    const std::vector<Common::Message> &messages,
    const std::map<std::string, std::string> &request_params,
    std::function<void(const std::string &, bool)> callback)
{
    const std::string tag = "Send_Message_Stream";

    if (!Is_Available()) {
        LOG_ERR("UnifiedProvider {}: model is not init", tag);
        callback("", true);
        return "";
    }

    // ---- 参数解析 ----
    double temperature = UNIFIED_DEFAULT_TEMPERATURE;
    int maxTokens = (apiStyle_ == ApiStyle::Responses)
                        ? UNIFIED_DEFAULT_MAX_OUTPUT
                        : UNIFIED_DEFAULT_MAX_TOKENS;

    auto it = request_params.find("temperature");
    if (it != request_params.end())
        temperature = std::stod(it->second);

    it = request_params.find("max_tokens");
    if (it != request_params.end())
        maxTokens = std::stoi(it->second);

    it = request_params.find("max_output_tokens");
    if (it != request_params.end())
        maxTokens = std::stoi(it->second);

    LOG_INFO("UnifiedProvider {}: model={}, temperature={}, maxTokens={}, msgCount={}",
             tag, modelName_, temperature, maxTokens, messages.size());

    // ---- 构建请求 ----
    Json::Value messagesJson = BuildMessagesJson(messages);
    std::string requestBody = BuildRequestBody(messagesJson, temperature,
                                               maxTokens, true);
    LOG_TRACE("UnifiedProvider {}: request body: {}", tag, requestBody);

    // ---- HTTP 客户端 ----
    LOG_DEBUG("UnifiedProvider {}: POST {} to {} (stream)", tag, GetApiPath(), baseURL_);
    httplib::Client client(baseURL_.c_str());
    client.set_connection_timeout(30, 0);   // 30 秒超时
    client.set_read_timeout(300, 0);        // 300 秒读取超时（流式）
    if (useProxy_)
        client.set_proxy(proxyHost_.c_str(), proxyPort_);

    httplib::Headers headers;
    headers.emplace("Authorization", "Bearer " + apiKey_);
    headers.emplace("Content-Type", "application/json");
    headers.emplace("Accept", "text/event-stream");

    httplib::Request req;
    req.method  = "POST";
    req.path    = GetApiPath();
    req.body    = requestBody;
    req.headers = headers;

    // ---- 状态追踪 ----
    int statusCode = -1;
    bool gotError = false;
    std::string errorMsg;

    req.response_handler = [&](const httplib::Response &response) {
        statusCode = response.status;
        if (statusCode != 200) {
            gotError = true;
            errorMsg = "HTTP Error:" + std::to_string(statusCode);
            return false;   // 终止请求
        }
        return true;        // 继续接收数据
    };

    // ---- SSE 缓冲区 ----
    std::string sseBuffer;
    bool streamCompleted = false;
    std::string fullResponse;

    req.content_receiver = [&](const char *data, uint64_t len,
                               uint64_t /*offset*/, uint64_t /*total_len*/) {
        if (gotError)
            return false;

        std::string chunk(data, len);
        LOG_INFO("UnifiedProvider {}: receive data: {}", tag, chunk);

        if (apiStyle_ == ApiStyle::Responses) {
            return HandleResponsesSSE(chunk, sseBuffer, fullResponse,
                                      streamCompleted, callback);
        } else {
            return HandleChatCompletionsSSE(chunk, sseBuffer, fullResponse,
                                            streamCompleted, callback);
        }
    };

    // ---- 发送请求 ----
    httplib::Response response;
    auto res = client.send(req, response);
    if (!res) {
        LOG_ERR("UnifiedProvider {}: request failed (connection) to {}",
                tag, baseURL_);
        callback("Failed to connect to " + baseURL_, true);
        return "";
    }

    if (gotError) {
        LOG_ERR("UnifiedProvider {}: request failed, status code: {}",
                tag, statusCode);
        callback(errorMsg, true);
        return errorMsg;
    }

    if (!streamCompleted) {
        LOG_WARN("UnifiedProvider {}: stream ended without completion marker",
                 tag);
        callback("", true);
    }

    LOG_INFO("UnifiedProvider {}: stream completed, total chars={}", tag, fullResponse.size());
    return fullResponse;
}
