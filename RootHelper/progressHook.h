#ifndef __RH_PROGRESS_HOOK__
#define __RH_PROGRESS_HOOK__

#include "unifiedlogging.h"

class ProgressHook {
public:
    const uint64_t totalSize;
    uint64_t currentSize;

    ProgressHook(uint64_t totalSize_) : totalSize(totalSize_), currentSize(0) {}

    virtual ~ProgressHook() = default;

    virtual void publish(uint64_t current) = 0;

    virtual void publishDelta(uint64_t delta) = 0;
};

class ConsoleProgressHook : public ProgressHook {
public:
    ConsoleProgressHook(uint64_t totalSize_) : ProgressHook(totalSize_) {
        PRINTUNIFIED("Total size: %" PRIu64 "\n", totalSize_);
    }

    void publish(uint64_t current) override {
        currentSize = current;
        SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %%", currentSize, ((100.0*currentSize)/totalSize));
    }

    void publishDelta(uint64_t delta) override {
        currentSize += delta;
        SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %%", currentSize, ((100.0*currentSize)/totalSize));
    }
};

#ifdef _WIN32

class MfcProgressHook : public ProgressHook {
public:
    ITaskbarList3* pTaskbarList;
    HWND hWnd;

    MfcProgressHook(HWND hWnd_, ITaskbarList3* pTaskbarList_, uint64_t totalSize_) : ProgressHook(totalSize_), hWnd(hWnd_), pTaskbarList(pTaskbarList_) {
        PRINTUNIFIED("Total size: %" PRIu64 "\n",totalSize_);
        CoInitialize(nullptr);
    }

    ~MfcProgressHook() override {
        // pTaskbarList->SetProgressState(hWnd,TBPF_NOPROGRESS);
        pTaskbarList->Release();
    }

    void publish(uint64_t current) override {
        currentSize = current;
        /*HRESULT hr = */pTaskbarList->SetProgressValue(hWnd, currentSize, totalSize);
        SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %%", currentSize, ((100.0*currentSize)/totalSize));
    }

    void publishDelta(uint64_t delta) override {
        currentSize += delta;
        /*HRESULT hr = */pTaskbarList->SetProgressValue(hWnd, currentSize, totalSize);
        SAMELINEPRINT("Progress: %" PRIu64 "\tPercentage: %.2f %%", currentSize, ((100.0*currentSize)/totalSize));
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
#endif

#endif /* __RH_PROGRESS_HOOK__ */