#ifndef COMMON_H
#define COMMON_H
#include "../util/nocopy.h"
#include <string>
#include <vector>
#include <ctime>

// // 条件编译：判断是否支持 C++17
// #if __cplusplus >= 201703L
//     #define HAS_CPP17 1
// #else
//     #define HAS_CPP17 0
// #endif

/**
 * @brief 公共结构体（为后续Json解析做准备）
 * 
 */

namespace ChatSDK::Common
{
    /**
     * @brief 消息类(用于存储UI界面的消息)
     * 
     */
    struct Message
    {
        public:

        Message(std::string role,std::string content):role_(role),content_(content),timestamp_(std::time(nullptr)){}
        std::string messageId_; // 消息id(自增)
        std::string sessionId_; // 会话id(自增)
        std::string role_;  // 角色，user/assistant/system(具体见ui设计，感觉不一定有该变量)
        std::string content_; // 消息内容
        std::time_t timestamp_; // 消息时间戳
    };


    #define MAX_TOKENS 1e4
    /**
     * @brief 配置类
     * 
     */
    
    // 模型的公共配置信息
    struct Config
    {
        public:
        std::string modelName_;         // 模型名称
        double temperature_ = 0.7;      // 温度值,用来控制生成文本的随机性
        int maxTokens_ = MAX_TOKENS;    // 最大token数
        virtual ~Config() = default;    // 添加虚函数的目的主要是为了实现:向下转型时的安全性
    };
    /**
    * @brief API模型配置类
    * 
    */
    struct APIConfig : public Config
    {
        public:
        std::string apiKey_;     // API密钥
        std::string baseURL_;    // API基础URL
        APIConfig() = default;
    };

    /**
     * @brief 模型配置记录（对应 ModelConfig 表中一行）
     *
     */
    struct ModelConfigRecord {
      std::string modelName_;     // 模型注册名称（同时也是 providers_ 的 key）
      std::string providerName_;  // provider 类型名（如 "unified"）
      std::string modelDesc_;     // 模型描述
      std::string baseURL_;       // API 端点
      std::string apiConfigJson_; // InitModel 的 params 序列化为 JSON
    };

    /**
     * @brief 模型持久化数据（供回调使用）
     *
     */
    struct ModelPersistenceData
    {
        std::string modelName;    // 模型注册名称（同时也是 providers_ 的 key）
        std::string modelDesc;    // 模型描述
        std::string baseURL;      // API 端点
        std::string paramsJson;   // InitModel 的 params 序列化为 JSON
    };
    /**
    * @brief 本地模型配置类
    * 
    */
    struct LocalConfig : public Config
    {
        public:
        std::string modelPath_;    // 模型路径
        LocalConfig() = default;
    };

    /**
     * @brief 模型消息类
     * 
     * 用于存储模型的元数据和状态
     * 
     */
    struct ModelInfo
    {
        public:
        std::string modelName_;         // 模型名称
        std::string modelDesc_;     // 模型描述
        std::string provider_;      // 模型提供者
        std::string baseURL_;      // 模型API endpoint base url
        bool isAvailable_ = false;  // 模型是否可用
        ModelInfo() = default;
        ModelInfo(const std::string modelName,const std::string modelDesc="",const std::string provider="",const std::string baseURL="")
        :modelName_(modelName),modelDesc_(modelDesc),provider_(provider),baseURL_(baseURL){}
    };


    /**
     * @brief 会话类
     * 
     * 用于存储会话中的消息列表
     * 
     */
    struct Session
    {
        public:
        std::string sessionId_;   // 会话ID(自增)
        std::string modelName_;   // 模型名称
        std::time_t timestamp_;   // 会话时间戳
        std::vector<Message> messages_;     // 会话中的消息列表
        Session(std::string modelName)
        :modelName_(modelName),timestamp_(std::time(nullptr))
        {}
    };
}
#endif


