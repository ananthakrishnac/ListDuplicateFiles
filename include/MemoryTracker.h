#pragma once

#include <string>
#include <map>
#include <mutex>
#include <psapi.h>
#include "Logger.h"

#pragma comment(lib, "psapi.lib")

// Forward declare so we can access Logger memory stats
class Logger;

class MemoryTracker {
public:
    static MemoryTracker& Instance() {
        static MemoryTracker instance;
        return instance;
    }

    // Call at start of scan
    void StartTracking() {
        std::lock_guard<std::mutex> lock(mtx);
        initialMemory = GetCurrentMemoryUsage();
        peakMemory = initialMemory;
        LOG_INFO("MemoryTracker: START - Initial memory: " + FormatBytes(initialMemory));
    }

    // Track allocation with unique ID
    void TrackAllocation(const std::string& label, size_t bytes) {
        std::lock_guard<std::mutex> lock(mtx);

        // Get or create unique ID for this label
        if (labelToId.find(label) == labelToId.end()) {
            labelToId[label] = ++nextTrackerId;
        }
        int trackerId = labelToId[label];

        // Increment allocation count for this label
        allocationCounts[label]++;
        int allocCount = allocationCounts[label];

        allocations[label] += bytes;
        totalAllocated += bytes;

        size_t currentMemory = GetCurrentMemoryUsage();
        if (currentMemory > peakMemory) {
            peakMemory = currentMemory;
        }

        // Format: ALLOC:XXXX (size, count)
        std::string trackStr = "ALLOC:" + PadNumber(trackerId, 4) + " (" +
                              FormatBytes(bytes) + ", " + std::to_string(allocCount) + ")";
        LOG_INFO(trackStr + " [" + label + "]");

        // Log if exceeds watermark (100MB)
        if (currentMemory > WATERMARK_BYTES) {
            LOG_WARNING("MemoryTracker ALERT: Memory exceeded 100MB watermark!");
            LOG_WARNING("  Current: " + FormatBytes(currentMemory) +
                       " | Peak: " + FormatBytes(peakMemory) +
                       " | Allocated: " + FormatBytes(totalAllocated) +
                       " | Label: " + label);
        }
    }

    // Track deallocation with unique ID
    void TrackDeallocation(const std::string& label, size_t bytes) {
        std::lock_guard<std::mutex> lock(mtx);

        // Get unique ID for this label
        int trackerId = 0;
        if (labelToId.find(label) != labelToId.end()) {
            trackerId = labelToId[label];
        }

        // Increment deallocation count for this label
        deallocationCounts[label]++;
        int deallocCount = deallocationCounts[label];

        allocations[label] -= bytes;
        totalDeallocated += bytes;

        size_t currentMemory = GetCurrentMemoryUsage();

        // Format: DE_ALLOC:XXXX (size, count)
        std::string trackStr = "DE_ALLOC:" + PadNumber(trackerId, 4) + " (" +
                              FormatBytes(bytes) + ", " + std::to_string(deallocCount) + ")";
        LOG_DEBUG(trackStr + " [" + label + "] | Current: " + FormatBytes(currentMemory));
    }

    // Get memory report
    void PrintReport() {
        std::lock_guard<std::mutex> lock(mtx);
        size_t currentMemory = GetCurrentMemoryUsage();

        LOG_INFO("========== MEMORY REPORT ==========");
        LOG_INFO("Initial Memory:      " + FormatBytes(initialMemory));
        LOG_INFO("Current Memory:      " + FormatBytes(currentMemory));
        LOG_INFO("Peak Memory:         " + FormatBytes(peakMemory));
        LOG_INFO("Total Allocated:     " + FormatBytes(totalAllocated));
        LOG_INFO("Total Deallocated:   " + FormatBytes(totalDeallocated));

        // Handle potential underflow
        size_t memIncrease = (currentMemory > initialMemory) ? (currentMemory - initialMemory) : 0;
        LOG_INFO("Memory Increase:     " + FormatBytes(memIncrease));
        std::string watermarkStatus = (currentMemory > WATERMARK_BYTES ? "EXCEEDED" : "OK");
        LOG_INFO("Watermark (100MB):   " + watermarkStatus);
        LOG_INFO("");
        LOG_INFO("ALLOC/DEALLOC Summary:");

        for (const auto& pair : labelToId) {
            const std::string& label = pair.first;
            int trackerId = pair.second;
            size_t netAlloc = allocations[label];
            int allocCount = allocationCounts[label];
            int deallocCount = deallocationCounts[label];

            std::string summary = "  [" + PadNumber(trackerId, 4) + "] " + label;
            summary += " | ALLOC_COUNT: " + std::to_string(allocCount);
            summary += " | DEALLOC_COUNT: " + std::to_string(deallocCount);
            summary += " | NET_MEMORY: " + FormatBytes(netAlloc);

            if (netAlloc > 0) {
                summary += " [LEAK]";
            }

            LOG_INFO(summary);
        }
        LOG_INFO("===================================");

        // Print Logger memory statistics (for information only - not part of system memory)
        // Note: Logger buffers ~10MB in RAM but writes to disk, so actual RAM usage is small
        LOG_INFO("");
        LOG_INFO("Logger Statistics (for reference only):");
        size_t logCount = Logger::Instance().GetLogEntryCount();
        size_t logMemory = Logger::Instance().GetTotalLogMemory();
        LOG_INFO("  Total Log Entries: " + std::to_string(logCount));
        LOG_INFO("  Total Log Data Written: " + FormatBytes(logMemory) + " (streamed to disk)");
        LOG_INFO("  Current Buffer Size: ~10 MB (flushed every 10MB)");
        if (logCount > 0) {
            LOG_INFO("  Average Entry Size: " + std::to_string(logMemory / logCount) + " bytes");
        }
        LOG_INFO("  Note: Logger memory is NOT included in allocation tracking (logs are streamed to disk)");
        LOG_INFO("");
    }

private:
    static const size_t WATERMARK_BYTES = 100 * 1024 * 1024; // 100MB

    MemoryTracker() = default;

    size_t GetCurrentMemoryUsage() const {
        PROCESS_MEMORY_COUNTERS pmc;
        if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
            return pmc.WorkingSetSize;
        }
        return 0;
    }

    std::string FormatBytes(size_t bytes) const {
        if (bytes < 1024) {
            return std::to_string(bytes) + " B";
        } else if (bytes < 1024 * 1024) {
            return std::to_string(bytes / 1024) + " KB";
        } else if (bytes < 1024 * 1024 * 1024) {
            return std::to_string(bytes / (1024 * 1024)) + " MB";
        } else {
            return std::to_string(bytes / (1024 * 1024 * 1024)) + " GB";
        }
    }

    // Pad number with leading zeros (e.g., 1 -> "0001")
    std::string PadNumber(int num, int width) const {
        std::string str = std::to_string(num);
        while (str.length() < static_cast<size_t>(width)) {
            str = "0" + str;
        }
        return str;
    }

    std::mutex mtx;
    std::map<std::string, size_t> allocations;
    std::map<std::string, int> labelToId;           // Maps label to unique tracker ID
    std::map<std::string, int> allocationCounts;    // Count of allocations per label
    std::map<std::string, int> deallocationCounts;  // Count of deallocations per label
    int nextTrackerId = 0;                          // Auto-incrementing tracker ID
    size_t initialMemory = 0;
    size_t peakMemory = 0;
    size_t totalAllocated = 0;
    size_t totalDeallocated = 0;
};

// Convenience macros for tracking
#define MEMORY_TRACK_ALLOC(label, bytes) MemoryTracker::Instance().TrackAllocation(label, bytes)
#define MEMORY_TRACK_FREE(label, bytes) MemoryTracker::Instance().TrackDeallocation(label, bytes)
#define MEMORY_START() MemoryTracker::Instance().StartTracking()
#define MEMORY_REPORT() MemoryTracker::Instance().PrintReport()
