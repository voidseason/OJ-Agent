#ifndef CHATSDK_LOG_H
#define CHATSDK_LOG_H
#ifndef SPDLOG_FMT_EXTERNAL
#define SPDLOG_FMT_EXTERNAL
#endif
#include <spdlog/spdlog.h>
#include <spdlog/logger.h>
#include <mutex>
#include "nocopy.h"
namespace ChatSDK::log
{
    /**
     * @brief 日志类
     * 
     * 用于封装spdlog
     * 
     */
    #define CHATSDK_LOG_LEVEL_DEFAULT spdlog::level::info
    class Logger: public nocopy::nocopy
    {
    public:
        static void initLogger(const std::string& loggerName,const std::string& loggerFile="stdout",spdlog::level::level_enum logLevel = CHATSDK_LOG_LEVEL_DEFAULT);
        static std::shared_ptr<spdlog::logger> getLogger();

    private:
        static std::shared_ptr<spdlog::logger> _logger;
        static std::mutex _mutex;
    };

    // 日志宏
    //std::string("[{:>10s}:{:<4d}]:左对齐10个字符,右对齐4个字符
    #define LOG_TRACE(format,...) ChatSDK::log::Logger::getLogger()->trace(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
    #define LOG_DEBUG(format,...) ChatSDK::log::Logger::getLogger()->debug(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
    #define LOG_INFO(format,...) ChatSDK::log::Logger::getLogger()->info(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
    #define LOG_WARN(format,...) ChatSDK::log::Logger::getLogger()->warn(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
    #define LOG_ERR(format,...) ChatSDK::log::Logger::getLogger()->error(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
    #define LOG_CRI(format,...) ChatSDK::log::Logger::getLogger()->critical(std::string("[{:>10s}:{:<4d}]") + format,__FILE__,__LINE__,##__VA_ARGS__)
}
#endif
