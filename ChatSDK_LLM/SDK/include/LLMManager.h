#pragma once
#include <unordered_map>
#include <memory>
#include <string>
#include <functional>
#include <vector>
#include "LLMProvider.h"

namespace ChatSDK {
    using namespace LLM_Provider;
    using namespace Common;

    /**
     * @brief LLM管理类
     * 
     */
    class LLMManager
    {
    public:
    //设置持久化回调（用于模型配置的保存/加载/删除）
    using SaveModelCb = std::function<bool(const ModelPersistenceData&)>;
    using LoadModelsCb = std::function<std::vector<ModelPersistenceData>()>;
    using DeleteModelCb = std::function<bool(const std::string&)>;
    void SetPersistenceCallbacks(SaveModelCb save_cb, LoadModelsCb load_cb, DeleteModelCb delete_cb);

    //注册LLM提供器
    bool RegisterProvider(std::string provider_name,
        std::unique_ptr<LLMProvider> provider);
    //初始化指定模型
    bool InitModel(const std::string &model_name,
        const std::map<std::string,std::string> &params);

    //获取可用模型列表
    std::vector<ModelInfo> GetAvailableModels() const;
    //检查模型是否可用
    bool IsModelAvailable(const std::string &model_name) const;
    //发送消息到指定模型
    std::string SendMessage(const std::string &model_name,
        const std::vector<Message> &messages,
        const std::map<std::string,std::string> &request_params);
    //发送流式消息到指定模型
    std::string SendMessageStreamed(const std::string &model_name,
        const std::vector<Message> &messages,
        const std::map<std::string,std::string> &request_params,
        std::function<void(const std::string&,bool)> callback
    );

    // === 模型配置持久化 ===
    //保存所有模型配置到数据库
    bool SaveModelsToDB() const;
    //从数据库加载所有模型配置并重新初始化
    bool LoadModelsFromDB();
    //删除指定模型配置（从内存和数据库移除）
    bool DeleteModel(const std::string &model_name);

  private:
    //将map参数序列化为JSON字符串
    static std::string ParamsToJson(const std::map<std::string,std::string> &params);
    //将JSON字符串反序列化为map
    static std::map<std::string,std::string> JsonToParams(const std::string &json_str);

  private:
    std::unordered_map<std::string, std::unique_ptr<LLMProvider>> providers_;
    std::unordered_map<std::string, ModelInfo> models_;
    SaveModelCb save_model_cb_;//保存模型配置回调
    LoadModelsCb load_models_cb_;//加载模型配置回调
    DeleteModelCb delete_model_cb_;//删除模型配置回调
    };

}