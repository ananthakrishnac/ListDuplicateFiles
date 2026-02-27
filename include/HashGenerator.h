#pragma once

#include <string>
#include <sstream>
#include <iomanip>

class HashGenerator {
public:
    // Simple hash function - not cryptographically secure but fast for duplicate detection
    static std::string GenerateSHA256(const std::string& input) {
        // Use a simple hash algorithm for fast duplicate detection
        // This is sufficient for identifying duplicate file metadata combinations
        unsigned long hash = 5381;

        for (unsigned char c : input) {
            hash = ((hash << 5) + hash) + c;  // hash * 33 + c
        }

        // Convert to 64-character hex string (simulating SHA256 format)
        std::ostringstream oss;
        oss << std::hex << std::setfill('0');

        // Generate 8 bytes (64 bits) of hash output
        for (int i = 0; i < 8; i++) {
            oss << std::setw(2) << ((hash >> (i * 4)) & 0xFF);
        }

        return oss.str();
    }

    // Generate hash for file metadata
    static std::string GenerateFileHash(const std::string& filename, const std::string& extension,
                                       long long fileSize, int imageWidth, int imageHeight) {
        std::ostringstream oss;
        oss << filename << "|"
            << extension << "|"
            << fileSize << "|"
            << imageWidth << "|"
            << imageHeight;

        return GenerateSHA256(oss.str());
    }
};
