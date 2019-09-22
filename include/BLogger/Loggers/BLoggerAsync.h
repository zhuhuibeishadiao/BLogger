#pragma once

#include <functional>
#include <memory>
#include <vector>
#include <deque>
#include <thread>
#include <mutex>
#include <unordered_map>

#include "BLogger/Loggers/BLoggerBase.h"
#include "BLogger/Loggers/FileHelper.h"
#include "BLogger/LogLevels.h"

namespace BLogger {

    enum class task_type : uint16_t
    {
        flush = 1,
        log   = 2
    };

    class task
    {
    private:
        task_type t;
    protected:
        task(task_type t)
            : t(t)
        {
        }
    public:
        task_type type()
        {
            return t;
        }
    };

    class log_task : public task
    {
    private:
        LogMsg msg;
    public:
        log_task(LogMsg&& msg)
            : task(task_type::log),
            msg(std::move(msg))
        {
        }

        LogMsg& message()
        {
            return msg;
        }
    };

    class flush_task : public task
    {
    public:
        flush_task()
            : task(task_type::flush)
        {
        }
    };

    #define BLOGGER_TASK_LIMIT 100

    class thread_pool
    {
    private:
        typedef std::shared_ptr<task>
            task_ptr;
        typedef std::lock_guard<std::mutex>
            locker;
        typedef std::unordered_map<
            uint16_t, std::shared_ptr<FileHelper>
        > logger_to_file;
        typedef std::unique_ptr<thread_pool>
            thread_pool_ptr;

        static thread_pool_ptr instance;
        logger_to_file m_Files;
        std::vector<std::thread> m_Pool;
        std::deque<task_ptr> m_TaskQueue;
        std::mutex m_QueueAccess;
        std::mutex m_GlobalWrite;
        bool m_Running;

        thread_pool(uint16_t thread_count)
            : m_Running(true)
        {
            m_Pool.reserve(std::thread::hardware_concurrency());

            for (uint16_t i = 0; i < thread_count; i++)
                m_Pool.emplace_back(std::bind(&thread_pool::worker, this));
        }

        thread_pool(const thread_pool& other) = delete;
        thread_pool(thread_pool&& other) = delete;

        thread_pool& operator=(thread_pool& other) = delete;
        thread_pool& operator=(thread_pool&& other) = delete;

        void worker()
        {
            bool did_work = do_work();

            while (m_Running || did_work)
            {
                if (!did_work)
                    std::this_thread::sleep_for(std::chrono::seconds(1));

                did_work = do_work();
            }
        }

        bool do_work()
        {
            task_ptr p;

            {
                locker lock(m_QueueAccess);

                if (!m_TaskQueue.empty())
                {
                    p = m_TaskQueue.front();
                    m_TaskQueue.pop_front();
                }
                else
                    return false;
            }

            if (p->type() == task_type::log)
            {

                log_task* task = static_cast<log_task*>(p.get());

                task->message().finalize_format();

                if (task->message().console())
                {
                    locker lock(m_GlobalWrite);

                    if (task->message().color())
                    {
                        switch (task->message().log_level())
                        {
                        case level::trace: set_output_color(BLOGGER_TRACE_COLOR); break;
                        case level::debug: set_output_color(BLOGGER_DEBUG_COLOR); break;
                        case level::info:  set_output_color(BLOGGER_INFO_COLOR);  break;
                        case level::warn:  set_output_color(BLOGGER_WARN_COLOR);  break;
                        case level::error: set_output_color(BLOGGER_ERROR_COLOR); break;
                        case level::crit:  set_output_color(BLOGGER_CRIT_COLOR);  break;
                        }
                    }

                    std::cout.write(task->message().data(), task->message().size());

                    if (task->message().color())
                        set_output_color(BLOGGER_RESET);
                }

                if (task->message().file())
                {
                    auto file = m_Files.find(task->message().sender());

                    if (file != m_Files.end()) 
                        file->second->write(task->message().data(), task->message().size());
                }
            }
            else if (p->type() == task_type::flush)
            {
                {
                    locker lock(m_GlobalWrite);

                    std::cout.flush();
                }
            }
            return true;
        }

        void shutdown()
        {
            m_Running = false;

            for (auto& worker : m_Pool)
                worker.join();
        }
    public:
        static thread_pool_ptr& get()
        {
            if (!instance)
            {
                instance.reset(new thread_pool(std::thread::hardware_concurrency()));
            }

            return instance;
        }

        void post(LogMsg&& message)
        {
            {
                locker lock(m_QueueAccess);

                if (m_TaskQueue.size() == BLOGGER_TASK_LIMIT)
                    m_TaskQueue.pop_front();

                m_TaskQueue.emplace_back(new log_task(std::move(message)));
            }
        }

        void flush()
        {
            locker lock(m_QueueAccess);
            m_TaskQueue.emplace_back(new flush_task());
        }

        void add_helper(uint16_t id, std::shared_ptr<FileHelper> fhptr)
        {
            m_Files[id] = fhptr;
        }

        void remove_helper(uint16_t id)
        {
            auto itr = m_Files.find(id);

            if (itr != m_Files.end())
                m_Files.erase(id);
        }

        ~thread_pool()
        {
            shutdown();
        }
    };

    thread_pool::thread_pool_ptr thread_pool::instance;

    class BLoggerAsync : public BLoggerBase
    {
    private:
        std::shared_ptr<FileHelper> m_File;
    public:
        BLoggerAsync()
            : BLoggerBase(),
            m_File(new FileHelper)
        {
            thread_pool::get()->add_helper(m_ID, m_File);
        }

        BLoggerAsync(const std::string& tag)
            : BLoggerBase(tag),
            m_File(new FileHelper)
        {
            thread_pool::get()->add_helper(m_ID, m_File);
        }

        BLoggerAsync(
            const std::string& tag,
            level lvl,
            bool default_pattern = false
        )
            : BLoggerBase(tag, lvl, default_pattern),
            m_File(new FileHelper)
        {
            thread_pool::get()->add_helper(m_ID, m_File);
        }

        void Flush() override
        {
            thread_pool::get()->flush();
        }

        ~BLoggerAsync() {}

        bool InitFileLogger(
            const std::string& directoryPath,
            size_t bytesPerFile,
            size_t maxLogFiles,
            bool rotateLogs = true
        ) override
        {
            m_File->init(
                directoryPath,
                m_Tag,
                bytesPerFile,
                maxLogFiles,
                rotateLogs
            );

            return m_File->ok();
        }

        bool EnableFileLogger() override
        {
            if (!(*m_File))
            {
                Error("Could not enable the file logger. Did you call InitFileLogger?");
                return false;
            }

            m_LogToFile = true;
            return true;
        }

        void TerminateFileLogger() override
        {
            m_File->terminate();
        }

        void SetTag(const std::string& tag) override
        {
            m_Tag = tag;
            SetPattern(m_CachedPattern);
            m_File->setTag(tag);
        }
    private:
        void Post(LogMsg&& msg) override
        {
            thread_pool::get()->post(std::move(msg));
        }
    };
}
