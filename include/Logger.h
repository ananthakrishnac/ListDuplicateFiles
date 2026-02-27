#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <fstream>
#include <sstream>
#include <ctime>
#include <windows.h>
#include <cstdlib>
#include <io.h>

class Logger {
public:
    static Logger& Instance() {
        static Logger instance;
        return instance;
    }

    void Log(const std::string& level, const std::string& message, const std::string& function = "", int line = 0) {
        if (!logFile.is_open()) {
            return;
        }

        // Check if this log level should be written based on build type
        // if (!ShouldLog(level)) {
        //     return;
        // }

        // Get current time
        time_t now = time(nullptr);
        struct tm timeinfo;
        localtime_s(&timeinfo, &now);
        char timeBuffer[20];
        strftime(timeBuffer, sizeof(timeBuffer), "%H:%M:%S", &timeinfo);

        // Calculate size of this log entry
        size_t entrySize = message.length() + level.length() + function.length() + 100;  // ~100 bytes for formatting

        // Track current in-memory buffer size (what's not yet flushed to disk)
        logBufferSize += entrySize;

        // For statistics: track total entries and total memory ever logged
        totalLogMemory += entrySize;
        logEntryCount++;

        // Write to file
        logFile << "[" << timeBuffer << "] [" << level << "]";
        if (!function.empty()) {
            logFile << " (" << function << ":" << line << ")";
        }
        logFile << " - " << message << std::endl;

        // Check if buffer exceeds 10MB threshold - if so, flush to disk
        if (logBufferSize >= 10 * 1024 * 1024) {  // 10MB threshold
            logFile.flush();

            // Force OS to write buffered data to disk
            HANDLE hFile = CreateFileA(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                      nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (hFile != INVALID_HANDLE_VALUE) {
                FlushFileBuffers(hFile);
                CloseHandle(hFile);
            }

            // After flush, the logs are on disk, so we can forget about the in-memory accumulation
            // Only keep track of current entries in buffer, not total ever written
            logBufferSize = 0;  // Reset buffer counter
            // Note: Don't reset totalLogMemory - keep cumulative count for statistics
        }
    }

    // Get memory statistics for Logger itself
    void PrintLogMemoryStats() {
        if (!logFile.is_open()) return;

        logFile << "\n=== LOGGER MEMORY STATISTICS ===" << std::endl;
        logFile << "Total Log Entries: " << logEntryCount << std::endl;
        logFile << "Total Log Memory: " << FormatBytes(totalLogMemory) << std::endl;
        logFile << "Average Entry Size: " << (logEntryCount > 0 ? totalLogMemory / logEntryCount : 0) << " bytes" << std::endl;
        logFile << "================================" << std::endl;
        logFile.flush();
    }

    void LogInfo(const std::string& msg, const std::string& func = "", int line = 0) {
        Log("INFO", msg, func, line);
    }

    void LogError(const std::string& msg, const std::string& func = "", int line = 0) {
        Log("ERROR", msg, func, line);
    }

    void LogWarning(const std::string& msg, const std::string& func = "", int line = 0) {
        Log("WARN", msg, func, line);
    }

    void LogDebug(const std::string& msg, const std::string& func = "", int line = 0) {
        Log("DEBUG", msg, func, line);
    }

    std::string GetLogPath() const {
        return logPath;
    }

    // Accessors for memory statistics
    size_t GetTotalLogMemory() const {
        return totalLogMemory;
    }

    size_t GetLogEntryCount() const {
        return logEntryCount;
    }

private:
    // Determine if a log level should be logged based on build type
    bool ShouldLog(const std::string& level) const {
#ifdef _DEBUG
        // In Debug build, log everything
        return true;
#else
        // In Release build, only log warnings and errors
        return (level == "WARN" || level == "ERROR");
#endif
    }

    // Helper to format bytes for display
    std::string FormatBytes(size_t bytes) {
        const char* units[] = {"B", "KB", "MB", "GB"};
        double size = static_cast<double>(bytes);
        int unitIndex = 0;
        while (size >= 1024 && unitIndex < 3) {
            size /= 1024;
            unitIndex++;
        }
        std::ostringstream oss;
        oss.precision(2);
        oss << std::fixed << size << " " << units[unitIndex];
        return oss.str();
    }

    Logger() {
        // Create log file in AppData\Local\FileExplorer using environment variable
        const char* appDataLocal = std::getenv("LOCALAPPDATA");
        if (appDataLocal) {
            logPath = std::string(appDataLocal) + "\\FileExplorer\\log_FileExplorer.txt";

            // Create directory if it doesn't exist
            std::string dirPath = std::string(appDataLocal) + "\\FileExplorer";
            CreateDirectoryA(dirPath.c_str(), nullptr);
        } else {
            logPath = "log_FileExplorer.txt";
        }

        logFile.open(logPath, std::ios::app);
        if (logFile.is_open()) {
            logFile << "\n\n=== FileExplorer Session Started ===" << std::endl;
        }
    }

    ~Logger() {
        if (logFile.is_open()) {
            logFile << "=== FileExplorer Session Ended ===" << std::endl;
            logFile.flush();
            logFile.close();
        }
    }

    std::ofstream logFile;
    std::string logPath;
    size_t logBufferSize = 0;      // Track buffer size for flushing every 10MB
    size_t totalLogMemory = 0;     // Total memory consumed by log entries
    size_t logEntryCount = 0;      // Number of log entries written
};

#ifdef _DEBUG
#define LOG_INFO(msg) Logger::Instance().LogInfo(msg, __FUNCTION__, __LINE__)
#define LOG_DEBUG(msg) Logger::Instance().LogDebug(msg, __FUNCTION__, __LINE__)
#else
#define LOG_INFO(msg) 
#define LOG_DEBUG(msg) 
#endif

#define LOG_WARNING(msg) Logger::Instance().LogWarning(msg, __FUNCTION__, __LINE__)
#define LOG_ERROR(msg) Logger::Instance().LogError(msg, __FUNCTION__, __LINE__)


#endif // LOGGER_H
