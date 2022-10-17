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
            lastPublished(0),
            progressThread(&ProgressHook::tfn, this) {}

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

    void tfn() {
        try {
            for(;;) {
                std::this_thread::sleep_for(samplingPeriod);
                uint64_t s = curSize;
                if(s == 0) continue; // wait a bit if no progress has been done yet
                if(s == -1) { // cannot send end-of-progress indication from here, it crashes with "pure virtual method call" in destructor; more info: https://stackoverflow.com/questions/962132/calling-virtual-functions-inside-constructors
                    lastPublished = s;
                    break;
                }
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
            }
        }
        catch(threadExitThrowable& i) {
            PRINTUNIFIED("ThreadExit invoked, exiting...\n");
        }
        SAMELINEPRINT("");
    }
};

class ConsoleProgressHook : public ProgressHook {
public:
    ConsoleProgressHook(uint64_t totalSize_) : ProgressHook(totalSize_) {
        // PRINTUNIFIED("Total size: %" PRIu64 "\n", totalSize_);
    }

    void doPublish() override {
        uint64_t lp = lastPublished;
        uint64_t ts = totalSize;
        SAMELINEPRINT("Progress: %" PRIu64 "  Percentage: %.2f %% of %" PRIu64 " bytes, speed: %.3f Mbps", lp, ((100.0f*lp)/ts), ts, curSpeed);
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
    IDescriptor& desc; // change to IDescriptor& if needed with other use cases than local or network unix socket

    LocalSocketProgressHook(uint64_t totalSize_, IDescriptor& desc_) : ProgressHook(totalSize_), desc(desc_) {
        // desc.writeAllOrExit(&totalSize, sizeof(uint64_t));
    }

    void doPublish() override {
        uint64_t lp = lastPublished;
        desc.writeAllOrExit(&lp, sizeof(uint64_t));
    }
};

#endif

#ifdef _WIN32
MfcProgressHook getProgressHook(uint64_t totalSize_) {
    return {console_hwnd,console_pTaskbarList,totalSize_};
}

std::unique_ptr<ProgressHook> getProgressHookP(uint64_t totalSize_) {
    return std::unique_ptr<ProgressHook>(new MfcProgressHook(console_hwnd,console_pTaskbarList,totalSize_));
}

// dummy, just to make build work on Windows
std::unique_ptr<ProgressHook> getLSProgressHookP(uint64_t totalSize_, IDescriptor& desc_) {
    return std::unique_ptr<ProgressHook>(nullptr);
}

#else
ConsoleProgressHook getProgressHook(uint64_t totalSize_) {
    return {totalSize_};
}

std::unique_ptr<ProgressHook> getProgressHookP(uint64_t totalSize_) {
    return std::unique_ptr<ProgressHook>(new ConsoleProgressHook(totalSize_));
}

std::unique_ptr<ProgressHook> getLSProgressHookP(uint64_t totalSize_, IDescriptor& desc_) {
    return std::unique_ptr<ProgressHook>(new LocalSocketProgressHook(totalSize_, desc_));
}
#endif

#endif /* __RH_PROGRESS_HOOK__ */
