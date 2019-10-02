#pragma once

#include <stdio.h>
#include <string>
#include <mutex>

#include "BLogger/OS/Functions.h"

namespace BLogger {

    class FileManager
    {
    private:
        FILE*       m_File;
        BLoggerString m_DirectoryPath;
        BLoggerString m_CachedTag;
        size_t      m_BytesPerFile;
        size_t      m_CurrentBytes;
        size_t      m_MaxLogFiles;
        size_t      m_CurrentLogFiles;
        bool        m_RotateLogs;
        bool        m_State;
        std::mutex  m_FileAccess;

        typedef std::lock_guard<std::mutex>
            locker;
    public:
        FileManager()
            : m_File(nullptr),
            m_DirectoryPath("none"),
            m_BytesPerFile(0),
            m_CurrentBytes(0),
            m_MaxLogFiles(0),
            m_CurrentLogFiles(0),
            m_RotateLogs(false),
            m_State(true)
        {
        }

        void setTag(BLoggerInString tag)
        {
            locker loc(m_FileAccess);
            m_CachedTag = tag;
        }

        bool init(
            BLoggerInString directoryPath,
            BLoggerInString loggerTag,
            size_t bytesPerFile,
            size_t maxLogFiles,
            bool rotateLogs = true
        )
        {
            {
                locker loc(m_FileAccess);

                m_CachedTag = loggerTag;

                m_BytesPerFile = bytesPerFile;
                m_MaxLogFiles = maxLogFiles;
                m_RotateLogs = rotateLogs;
                m_CurrentBytes = 0;
                m_CurrentLogFiles = 1;

                m_DirectoryPath = directoryPath;
                m_DirectoryPath += '/';

                newLogFile();
            }
           

            if (m_File)
                return true;

            return false;
        }

        void terminate()
        {
            locker lock(m_FileAccess);

            if (m_File)
            {
                fclose(m_File);
                m_File = nullptr;
            }
        }

        bool ok()
        {
            return m_State;
        }

        void write(void* data, size_t size)
        {
            locker lock(m_FileAccess);

            if (!ready())
                return;

            ++size;

            if (m_BytesPerFile && size > m_BytesPerFile)
                return;

            if (m_BytesPerFile && (m_CurrentBytes + size) > m_BytesPerFile)
            {
                if (m_CurrentLogFiles == m_MaxLogFiles)
                {
                    if (!m_RotateLogs)
                        return;
                    else
                    {
                        m_CurrentLogFiles = 1;
                        m_CurrentBytes = 0;
                        newLogFile();
                    }
                }
                else
                {
                    m_CurrentBytes = 0;
                    ++m_CurrentLogFiles;
                    newLogFile();
                }
            }

            m_CurrentBytes += size;

            fwrite(data, 1, size - 1, m_File);
        }

        void flush()
        {
            locker lock(m_FileAccess);

            if (m_File)
                fflush(m_File);
        }

        operator bool()
        {
            locker lock(m_FileAccess);

            return ok() && ready();
        }

        bool ready()
        {
            return static_cast<bool>(m_File);
        }

        ~FileManager()
        {
            if (m_File)
                fclose(m_File);
        }
    private:
        void constructFullPath(
            BLoggerString& outPath
        )
        {
            outPath += m_DirectoryPath;
            outPath += m_CachedTag;
            outPath += '-';
            outPath += std::to_string(m_CurrentLogFiles);
            outPath += ".log";
        }

        void newLogFile()
        {
            if (m_File)
            {
                fclose(m_File);
                m_File = nullptr;
            }

            BLoggerString fullPath;
            constructFullPath(fullPath);

            OPEN_FILE(m_File, fullPath);

            m_File ? m_State = true : m_State = false;
        }
    };
}