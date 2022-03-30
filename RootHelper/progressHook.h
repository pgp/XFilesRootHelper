#ifndef __RH_PROGRESS_HOOK__
#define __RH_PROGRESS_HOOK__

#include "unifiedlogging.h"
#include <chrono>

// TODO refactor using non-virtual methods publish and publishDelta, and virtual method doPublish (chain-of-responsibility)

constexpr auto samplingPeriodMs = 250;
constexpr auto samplingPeriod = std::chrono::milliseconds(samplingPeriodMs);

class ProgressHook {
public:
    const uint64_t totalSize;
    uint64_t lastPublished;
    /*volatile*/ uint64_t curSize;
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

    /*float getCurrentSpeedMbps(uint64_t p1) {
        auto&& t = std::chrono::steady_clock::now();
        std::chrono::duration<float> diff = t - lastT;
        lastT = t;
        auto ret = (p1 - lastPublished) *1.0f / (diff.count() * 1000000.0f);
        lastPublished = p1;
        return ret;
    }*/

    // not working, hangs on ctor
    void start() {
        progressThread = std::thread( [this] {
            std::this_thread::sleep_for(samplingPeriod);
            for(;;) {
                uint64_t s = curSize;
                if(s == -1) break; // end-of-progress indication // TODO remember to set it everywhere!
                auto ds = s - lastPublished;
                lastPublished = s;
                curSpeed = (ds*1.0f)/(samplingPeriodMs*1000.0f); // Mbps
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

#endif

#ifdef _WIN32
MfcProgressHook getProgressHook(uint64_t totalSize_) {
    return {console_hwnd,console_pTaskbarList,totalSize_};
}
#else
ConsoleProgressHook getProgressHook(uint64_t totalSize_) {
    return {totalSize_};
}
#endif

#endif /* __RH_PROGRESS_HOOK__ */
