#ifndef LLM_PROVIDER_H
#define LLM_PROVIDER_H

#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../include/common.h"
namespace ChatSDK::LLM_Provider
{

    /**
     * @brief LLM提供器类
     * 
     * LLM提供器(策略模式)
     * @param apiKey API密钥
     * @param baseURL 基础URL
     */
    class LLMProvider
    {
    public:
        // 初始化模型
        virtual bool Init_Model(const std::map<std::string, std::string>& model_config) = 0;
        // 模型是否可用
        virtual bool Is_Available() const = 0;
        // 模型名称
        virtual std::string Model_Name() const = 0;
        // 模型描述
        virtual std::string Model_Desc() const = 0;
        // 发送消息
        virtual std::string Send_Message(const std::vector<Common::Message>& messages,
            const std::map<std::string, std::string>& request_params) = 0;
        // 发送流式消息
        virtual std::string Send_Message_Stream(const std::vector<Common::Message>& messages,
            const std::map<std::string, std::string>& request_params,
            std::function<void(const std::string&,bool)> callback
        ) = 0;

        protected:
        std::string apiKey_; // API密钥
        std::string baseURL_; // 基础URL
        bool isAvailable_; // 模型是否可用
    };
}

#endif