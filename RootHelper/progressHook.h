#ifndef __RH_PROGRESS_HOOK__
#define __RH_PROGRESS_HOOK__

#include "unifiedlogging.h"
#include <chrono>

// TODO refactor using non-virtual methods publish and publishDelta, and virtual method doPublish (chain-of-responsibility)

constexpr uint32_t DEFAULT_PROGRESSHOOK_THROTTLE = 33554432; // 32 Mb, equal to COPY_CHUNK_SIZE in common_uds.h
class ProgressHook {
public:
    const uint64_t totalSize;
    const uint32_t throttle; // progress will be actually published every throttle bytes
    uint64_t lastPublished;
    std::chrono::time_point<std::chrono::steady_clock> lastT;
    uint64_t currentSize;

    ProgressHook(uint64_t totalSize_, uint32_t throttle_) :
            totalSize(totalSize_),
            throttle(throttle_),
            currentSize(0),
            lastPublished(0),
            lastT(std::chrono::steady_clock::now()) {}

    virtual ~ProgressHook() {
        // this assumes no other console messages are written between last progress and progress hook destructor;
        // in such case, this would delete previous message from the console, if such message does not end with \n
        SAMELINEPRINT("");
    }

    virtual void publish(uint64_t current) = 0;

    virtual void publishDelta(uint64_t delta) = 0;

    float getCurrentSpeedMbps(uint64_t p1) {
        auto&& t = std::chrono::steady_clock::now();
        std::chrono::duration<float> diff = t - lastT;
        lastT = t;
        auto ret = (p1 - lastPublished) *1.0f / (diff.count() * 1000000.0f);
        lastPublished = p1;
        return ret;
    }
};

class ConsoleProgressHook : public ProgressHook {
public:
    ConsoleProgressHook(uint64_t totalSize_, uint32_t throttle_) : ProgressHook(totalSize_, throttle_) {
        // PRINTUNIFIED("Total size: %" PRIu64 "\n", totalSize_);
    }

    void publish(uint64_t current) override {
        currentSize = current;
        if(currentSize - lastPublished >= throttle) {
            auto curSpeed = getCurrentSpeedMbps(currentSize);
            SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", currentSize, ((100.0*currentSize)/totalSize), totalSize, curSpeed);
        }
    }

    void publishDelta(uint64_t delta) override {
        currentSize += delta;
        if(currentSize - lastPublished >= throttle) {
            auto curSpeed = getCurrentSpeedMbps(currentSize);
            SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", currentSize, ((100.0*currentSize)/totalSize), totalSize, curSpeed);
        }
    }
};

#ifdef _WIN32

class MfcProgressHook : public ConsoleProgressHook {
public:
    ITaskbarList3* pTaskbarList;
    HWND hWnd;

    MfcProgressHook(HWND hWnd_, ITaskbarList3* pTaskbarList_, uint64_t totalSize_, uint32_t throttle_) :
            ConsoleProgressHook(totalSize_, throttle_), hWnd(hWnd_), pTaskbarList(pTaskbarList_) {
        // PRINTUNIFIED("Total size: %" PRIu64 "\n",totalSize_);
        CoInitialize(nullptr);
    }

    // ~MfcProgressHook() override {
        // pTaskbarList->SetProgressState(hWnd,TBPF_NOPROGRESS);
        // pTaskbarList->Release(); // releasing pTaskbarList twice (in x0.at upload for example, when two progress hooks are used) causes access violation on windows 7 (works fine in win8 and win10 instead)
    // }

    void publish(uint64_t current) override {
        currentSize = current;
        if(currentSize - lastPublished >= throttle) {
            auto curSpeed = getCurrentSpeedMbps(currentSize);
            /*HRESULT hr = */pTaskbarList->SetProgressValue(hWnd, currentSize, totalSize);
            SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", currentSize, ((100.0*currentSize)/totalSize), totalSize, curSpeed);
        }
    }

    void publishDelta(uint64_t delta) override {
        currentSize += delta;
        if(currentSize - lastPublished >= throttle) {
            auto curSpeed = getCurrentSpeedMbps(currentSize);
            /*HRESULT hr = */pTaskbarList->SetProgressValue(hWnd, currentSize, totalSize);
            SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", currentSize, ((100.0*currentSize)/totalSize), totalSize, curSpeed);
        }
    }
};

#endif

#ifdef _WIN32
MfcProgressHook getProgressHook(uint64_t totalSize_, uint32_t throttle_ = DEFAULT_PROGRESSHOOK_THROTTLE) {
    return {console_hwnd,console_pTaskbarList,totalSize_,throttle_};
}
#else
ConsoleProgressHook getProgressHook(uint64_t totalSize_, uint32_t throttle_ = DEFAULT_PROGRESSHOOK_THROTTLE) {
    return {totalSize_,throttle_};
}
#endif

#endif /* __RH_PROGRESS_HOOK__ */
