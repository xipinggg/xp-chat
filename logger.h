#ifndef LOGGER_H_
#define LOGGER_H_

#include <iostream>
#include <string>
#include <experimental/source_location>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <filesystem>
#include <list>
#include <vector>
#include <mutex>
#include <ctime>
#include <sys/uio.h>
#include <sys/time.h>
#include <fmt/format.h>
#include "thread_tools.h"

namespace xp
{
    class Logger;
}
extern xp::Logger *logger;
namespace xp
{
    auto make_str_time(const std::time_t t = std::time(nullptr)) noexcept
    {
        constexpr size_t maxsize = 20;
        std::string str(maxsize, '\0');
        const char *format = "%F %T";
        std::strftime(str.data(), maxsize, format, std::localtime(&t));
        return str;
    }

    std::string make_log(const std::string_view message,
                         const std::string_view level,
                         const std::time_t t,
                         const std::experimental::source_location location = std::experimental::source_location::current()) noexcept
    {
        std::string log{};
        try
        {
            auto time_str = make_str_time(t);
            auto time_sv = std::string_view{time_str.data(), time_str.size() - 1};
            auto tid = pthread_self();
            log = fmt::format("[{}] ## {}\n    time : {}\n    pthread self : {}\n    line and func : {}  {}\n    file : {}\n",
                              level, message, time_sv, tid,
                              location.function_name(),
                              location.line(),
                              location.file_name());
        }
        catch (...)
        {
        }
        return log;
    }

    constexpr bool log_filter(const std::string_view level) noexcept
    {
        return level == "trace";
    }

    void log(const std::string_view message = "",
             const std::string_view level = "trace",
             const std::time_t t = std::time(nullptr),
             const std::experimental::source_location location = std::experimental::source_location::current()) noexcept;

    //template <lock_type Lock = xp::SpinLock>
    class Logger
    {
        public:
        Logger() noexcept : path_{std::filesystem::current_path() / "logs"}
        {
            if (!std::filesystem::exists(path_))
            {
                std::filesystem::create_directory(path_);
            }
        }
        Logger(std::filesystem::path path) noexcept : path_{path}
        {
            if (!std::filesystem::exists(path_))
            {
                std::filesystem::create_directory(path_);
            }
        }
    
        using location_type = std::experimental::source_location;
        using log_type = std::string;
        friend class xp::Singleton<Logger>;
        
        ~Logger() = default;

        bool commit(const std::string_view message = "", const std::string_view level = "trace",
                    std::time_t t = std::time(nullptr),
                    const location_type location = std::experimental::source_location::current()) noexcept
        {
            if (auto log = make_log(message, level, t, location);
				log != "") [[likely]]
			{
                //if(level != "trace")
                {
                    //std::cout << log;
                }
                logs_.add(std::move(log));
				return true;
			}
			return false;
        }
        void output()
        {
            if (logs_.empty())
            {
                return;
            }
            auto logs = logs_.get();
            std::ofstream fout(path_ / "test.log", std::ios::app | std::ios::out);
            if (fout.bad())
            {
                return;
            }
            for (auto &log : logs)
            {
                fout << log;
            }
            fout.close();
        }/*
        int output2()
		{
			auto logs = logs_.get();
			iovec_.reserve(logs.size());
			iovec_.clear();
			for (auto &log : logs)
			{
				iovec_.push_back({log.data(), log.size()});
			}
			auto size = iovec_.size();
			//if (size > IOV_MAX)
			//{
				//int count = IOV_MAX;
				//size -= IOV_MAX;
				int res = ::writev(fd_, iovec_.data(), size);
			//}

			return logs.size();
		}*/
    private:
		int num_ = 0;
		xp::SwapBuffer<log_type> logs_;
		std::filesystem::path path_ = std::filesystem::current_path() / "logs";
    };

    void log(const std::string_view message,
             const std::string_view level,
             const std::time_t t,
             const std::experimental::source_location location) noexcept
    {
        
        if (!log_filter(level))
            logger->commit(message, level, t, location);
    }
}

#endif //LOGGER_H_