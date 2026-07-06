#include "../include/LLMManager.h"
#include "../include/UnifiedProvider.h"
#include "../util/log.h"
#include <json/json.h>

namespace ChatSDK {
    using namespace LLM_Provider;
    using namespace Common;

    void LLMManager::SetPersistenceCallbacks(
        SaveModelCb save_cb, LoadModelsCb load_cb, DeleteModelCb delete_cb) {
        save_model_cb_   = std::move(save_cb);
        load_models_cb_  = std::move(load_cb);
        delete_model_cb_ = std::move(delete_cb);
    }

    // 注册LLM提供程序
    bool LLMManager::RegisterProvider(std::string provider_name,
                          std::unique_ptr<LLMProvider> provider) {
        if(!provider) {
            LOG_ERR("LLMManager RegisterProvider: provider is null");
            return false;
        }
        providers_[provider_name] = std::move(provider);
        LOG_INFO("LLMManager RegisterProvider: provider name {} registered",
                 provider_name);
        return true;
    }

    // 初始化指定模型
    bool LLMManager::InitModel(const std::string &model_name,
                   const std::map<std::string, std::string> &params)
    {
        auto it = providers_.find(model_name);
        if (it == providers_.end())
        {
            LOG_ERR("LLMManager InitModel: provider '{}' not registered",
                   model_name);
            return false;
        }

        // 1. 先初始化 provider（调用 Init_Model 设置 apiKey、endpoint、isAvailable 等）
        if (!it->second->Init_Model(params)) {
            LOG_ERR("LLMManager InitModel: Init_Model failed for '{}'", model_name);
            models_[model_name].isAvailable_ = false;
            return false;
        }

        // 2. 检查 availability
        bool available = it->second->Is_Available();
        models_[model_name].isAvailable_ = available;

        // 3. 填充 ModelInfo 字段
        models_[model_name].modelName_ = model_name;
        models_[model_name].modelDesc_ = it->second->Model_Desc();
        models_[model_name].provider_  = model_name;
        {
            auto ep_it = params.find("endpoint");
            if (ep_it == params.end()) ep_it = params.find("base_url");
            if (ep_it != params.end()) models_[model_name].baseURL_ = ep_it->second;
        }

        if (!available) {
            LOG_ERR("LLMManager InitModel: model '{}' not available after init", model_name);
            return false;
        }

        LOG_INFO("LLMManager InitModel: model '{}' init success", model_name);

        // 4. 持久化到数据库
        if (save_model_cb_) {
            ModelPersistenceData data;
            data.modelName  = model_name;
            data.modelDesc  = it->second->Model_Desc();
            data.baseURL    = models_[model_name].baseURL_;
            data.paramsJson = ParamsToJson(params);
            save_model_cb_(data);
        }

        return true;
    }       
        //获取可用模型列表
        std::vector<ModelInfo> LLMManager::GetAvailableModels() const {
            std::vector<ModelInfo> AvailableModels;
            for (auto &model : models_) {
                if (model.second.isAvailable_) {
                    AvailableModels.push_back(model.second);
                }
            }
            return AvailableModels;
        }
        //检查模型是否可用
        bool LLMManager::IsModelAvailable(const std::string &model_name) const {
            auto it =models_.find(model_name);
            if(it == models_.end()) {
                return false;
            }
            return it->second.isAvailable_;
        }

        //发送消息到指定模型
        std::string LLMManager::SendMessage(const std::string &model_name,
            const std::vector<Message> &messages,
            const std::map<std::string,std::string> &request_params) {
            auto it = providers_.find(model_name);
            if(it==providers_.end())
            {
                LOG_ERR("LLMManager SendMessage: provider name {} not registered",
                       model_name);
                return "";
            }

            if(!it->second->Is_Available()) {
                LOG_ERR("LLMManager SendMessage: model {} is not available",
                       model_name);
                return "";
            }
            return it->second->Send_Message(messages, request_params);
        }

        //发送流式消息到指定模型
        std::string LLMManager::SendMessageStreamed(const std::string &model_name,
            const std::vector<Message> &messages,
            const std::map<std::string,std::string> &request_params,
            std::function<void(const std::string&,bool)> callback)
        {
            auto it = providers_.find(model_name);
            if(it==providers_.end())
            {
                LOG_ERR("LLMManager SendStreamMessage: provider name {} not registered",
                       model_name);
                return "";
            }

            if(!it->second->Is_Available()) {
                LOG_ERR("LLMManager SendStreamMessage: model {} is not available",
                       model_name);
                return "";
            }
            return it->second->Send_Message_Stream(messages, request_params, callback);
        }

        // =============================================================
        //  模型配置持久化
        // =============================================================

        std::string LLMManager::ParamsToJson(
            const std::map<std::string, std::string> &params)
        {
            Json::Value root(Json::objectValue);
            for (const auto &[key, val] : params) {
                root[key] = val;
            }
            Json::FastWriter writer;
            return writer.write(root);
        }

        std::map<std::string, std::string> LLMManager::JsonToParams(
            const std::string &json_str)
        {
            std::map<std::string, std::string> params;
            Json::Value root;
            Json::Reader reader;
            if (!reader.parse(json_str, root)) {
                LOG_ERR("LLMManager JsonToParams: failed to parse JSON");
                return params;
            }
            for (const auto &key : root.getMemberNames()) {
                if (root[key].isString()) {
                    params[key] = root[key].asString();
                }
            }
            return params;
        }

        bool LLMManager::SaveModelsToDB() const
        {
            if (!save_model_cb_) {
                LOG_ERR("LLMManager SaveModelsToDB: save callback is not set");
                return false;
            }
            for (const auto &[name, model] : models_) {
                ModelPersistenceData data;
                data.modelName  = name;
                data.modelDesc  = model.modelDesc_;
                data.baseURL    = model.baseURL_;
                data.paramsJson = "{}";
                if (!save_model_cb_(data)) {
                    LOG_ERR("LLMManager SaveModelsToDB: failed to save {}", name);
                    return false;
                }
            }
            LOG_INFO("LLMManager SaveModelsToDB: {} models saved", models_.size());
            return true;
        }

        bool LLMManager::LoadModelsFromDB()
        {
            if (!load_models_cb_) {
                LOG_WARN("LLMManager LoadModelsFromDB: load callback is not set");
                return false;
            }
            auto records = load_models_cb_();
            size_t loaded = 0;
            for (const auto &data : records) {
                // 如果 provider 不存在，自动用 UnifiedProvider 注册
                if (providers_.find(data.modelName) == providers_.end()) {
                    RegisterProvider(data.modelName,
                        std::make_unique<UnifiedProvider>());
                    LOG_INFO("LLMManager LoadModelsFromDB: auto-registered provider for '{}'",
                             data.modelName);
                }
                auto params = JsonToParams(data.paramsJson);
                if (InitModel(data.modelName, params)) {
                    loaded++;
                }
            }
            LOG_INFO("LLMManager LoadModelsFromDB: {}/{} models loaded",
                     loaded, records.size());
            return true;
        }

        bool LLMManager::DeleteModel(const std::string &model_name)
        {
            // 从内存移除
            models_.erase(model_name);
            providers_.erase(model_name);
            // 通过回调删除持久化数据
            if (delete_model_cb_) {
                return delete_model_cb_(model_name);
            }
            LOG_INFO("LLMManager DeleteModel: {} removed", model_name);
            return true;
        }
}
