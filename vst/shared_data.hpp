#pragma once

/**
 * Shared data between VST3 processor and editor
 * Uses a simple thread-safe buffer for spectrum data
 */

#include <vector>
#include <mutex>
#include <atomic>

namespace SpectrumEQ {

class SharedSpectrumData {
public:
    static SharedSpectrumData& instance() {
        static SharedSpectrumData instance;
        return instance;
    }
    
    void setSpectrum(const std::vector<float>& spectrum) {
        std::lock_guard<std::mutex> lock(mutex_);
        spectrum_ = spectrum;
        hasNewData_.store(true);
    }
    
    bool getSpectrum(std::vector<float>& outSpectrum) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (spectrum_.empty()) return false;
        outSpectrum = spectrum_;
        hasNewData_.store(false);
        return true;
    }
    
    bool hasNewData() const {
        return hasNewData_.load();
    }
    
private:
    SharedSpectrumData() = default;
    
    std::vector<float> spectrum_;
    std::mutex mutex_;
    std::atomic<bool> hasNewData_{false};
};

} // namespace SpectrumEQ

