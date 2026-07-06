#include "../util/log.h"
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/async.h>


namespace ChatSDK::log
{
    std::shared_ptr<spdlog::logger> Logger::_logger = nullptr;
    std::mutex Logger::_mutex;

    /**
     * @brief 初始化日志器
     * 
     * @param loggerName 日志器名称
     * @param loggerFile 日志文件路径，默认值为"stdout"，表示输出到控制台
     * @param logLevel 日志级别，默认值为CHATSDK_LOG_LEVEL_DEFAULT
     */
    void Logger::initLogger(const std::string& loggerName,const std::string& loggerFile,spdlog::level::level_enum logLevel)
    {
        if(nullptr == _logger)
        {
            std::lock_guard<std::mutex> lock(_mutex);
            if(nullptr == _logger)
            {
                // 设置全局自动刷新级别，当日志级别 >= logLevel时，日志会被立即刷新到文件
                spdlog::flush_on(logLevel);
                // 启用异步日志，即将日志存放到队列中，由后台线程负责写入
                // 参数1：队列的大小，参数2：后台线程数量
                spdlog::init_thread_pool(32768,1);
                // 如果loggerFile为空字符串或"stdout"，则创建一个带颜色的输出到控制台的日志器
                if(loggerFile.empty() || "stdout" == loggerFile)
                {
                    // 创建一个带颜色的输出到控制台的日志器
                    _logger = spdlog::stdout_color_mt(loggerName);
                }
                else
                {
                    // 创建一个文件输出的日志器，日志会被写入到指定文件中
                    _logger = spdlog::basic_logger_mt<spdlog::async_factory>(loggerName,loggerFile);

                }
            }

            /**
             * @brief 设置日志格式
             * 
             * 格式为：[时分秒][日志器名称][日志级别] 日志消息
             */
            _logger->set_pattern("[%H:%M:%S][%n][%-7l]%v");
            _logger->set_level(logLevel);
        }
    }
    /**
     * @brief 获取日志器
     * 
     * @return std::shared_ptr<spdlog::logger> 日志器指针
     */
    std::shared_ptr<spdlog::logger> Logger::getLogger()
    {
        return _logger;
    }
}

