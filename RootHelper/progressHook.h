#ifndef __RH_PROGRESS_HOOK__
#define __RH_PROGRESS_HOOK__

#include "unifiedlogging.h"
#include <chrono>

constexpr auto samplingPeriodMs = 250;
constexpr auto samplingPeriod = std::chrono::milliseconds(samplingPeriodMs);

class ProgressHook {
public:
    const uint64_t totalSize;
    uint64_t lastPublished;
    uint64_t curSize;
    uint64_t lastNonZeroDs;
    std::chrono::time_point<std::chrono::high_resolution_clock> lastT = std::chrono::high_resolution_clock::now();
    float curSpeed;

    std::thread progressThread;

    ProgressHook(uint64_t totalSize_) :
            totalSize(totalSize_),
            curSize(0),
            lastPublished(0) {
        start();
    }

    virtual ~ProgressHook() {
        // this assumes no other console messages are written between last progress and progress hook destructor;
        // in such case, this would delete previous message from the console, if such message does not end with \n
        curSize = -1;
        progressThread.join();
    }

    void publish(uint64_t current) {
        curSize = current;
    }

    void publishDelta(uint64_t delta) {
        curSize += delta;
    }

    virtual void doPublish() = 0;

    void start() {
        progressThread = std::thread([this] {
            std::this_thread::sleep_for(samplingPeriod);
            for(;;) {
                uint64_t s = curSize;
                if(s == -1) break; // end-of-progress indication
                auto ds = s - lastPublished;

                auto t = std::chrono::high_resolution_clock::now();
                float diff = std::chrono::duration<float, std::micro>(t - lastT).count();

                if(ds != 0) {
                    lastNonZeroDs = ds;
                    lastPublished = s;
                    lastT = t;
                }
                // when progress is stale, show a decreasing "instantaneous" speed
                curSpeed = (lastNonZeroDs*1.0f)/diff; // bytes per microsecond, a.k.a. megabytes per second

                doPublish();
                std::this_thread::sleep_for(samplingPeriod);
            }
            SAMELINEPRINT("");
        });
    }
};

class ConsoleProgressHook : public ProgressHook {
public:
    ConsoleProgressHook(uint64_t totalSize_) : ProgressHook(totalSize_) {
        // PRINTUNIFIED("Total size: %" PRIu64 "\n", totalSize_);
    }

    void doPublish() override {
        SAMELINEPRINT("Progress: %" PRIu64 "  Percentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", lastPublished, ((100.0f*lastPublished)/totalSize), totalSize, curSpeed);
    }
};

#ifdef _WIN32

class MfcProgressHook : public ConsoleProgressHook {
public:
    ITaskbarList3* pTaskbarList;
    HWND hWnd;

    MfcProgressHook(HWND hWnd_, ITaskbarList3* pTaskbarList_, uint64_t totalSize_) :
            ConsoleProgressHook(totalSize_), hWnd(hWnd_), pTaskbarList(pTaskbarList_) {
        // PRINTUNIFIED("Total size: %" PRIu64 "\n",totalSize_);
        CoInitialize(nullptr);
    }

    // ~MfcProgressHook() override {
        // pTaskbarList->SetProgressState(hWnd,TBPF_NOPROGRESS);
        // pTaskbarList->Release(); // releasing pTaskbarList twice (in x0.at upload for example, when two progress hooks are used) causes access violation on windows 7 (works fine in win8 and win10 instead)
    // }

    void doPublish() override {
        /*HRESULT hr = */pTaskbarList->SetProgressValue(hWnd, curSize, totalSize);
        SAMELINEPRINT("Progress: %" PRIu64 "  Percentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", lastPublished, ((100.0f*lastPublished)/totalSize), totalSize, curSpeed);
    };
};

#else

class LocalSocketProgressHook : public ProgressHook {
public:
    PosixDescriptor& desc; // change to IDescriptor& if needed with other use cases than local or network unix socket

    LocalSocketProgressHook(uint64_t totalSize_, PosixDescriptor& desc_) : ProgressHook(totalSize_), desc(desc_) {
        desc.writeAllOrExit(&totalSize, sizeof(uint64_t));
    }

    void doPublish() override {
        desc.writeAllOrExit(&lastPublished, sizeof(uint64_t));
    }
};

#endif

#ifdef _WIN32
MfcProgressHook getProgressHook(uint64_t totalSize_) {
    return {console_hwnd,console_pTaskbarList,totalSize_};
}
#else
ConsoleProgressHook getProgressHook(uint64_t totalSize_) {
    return {totalSize_};
}

LocalSocketProgressHook getLSProgressHook(uint64_t totalSize_, PosixDescriptor& desc_) {
    return {totalSize_, desc_};
}
#endif

#endif /* __RH_PROGRESS_HOOK__ */
