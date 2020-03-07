#ifndef RELIABLERINGBUFFER_H
#define RELIABLERINGBUFFER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <vector>

#include "desc/IDescriptor.h"

constexpr unsigned DEFAULT_CAPACITY = 1048576;
//constexpr unsigned DEFAULT_CAPACITY = 32*1048576;

class RingBuffer : public IDescriptor {
private:
    std::atomic_bool closed;
    const int capacity;
    uint8_t* ringbuffer;
    int readIdx = 0;
    int writeIdx = 0;
    std::mutex ringbuffer_mutex;
    std::condition_variable ringbuffer_written_cond;
    std::condition_variable ringbuffer_read_cond;

    // how many bytes can be read before readIdx reaches writeIdx, assuming clockwise read direction
    inline int oneDirCircularDistance() {
        return (writeIdx - readIdx + capacity)%capacity;
    }

    // always increment in one direction along the circle
    inline void incrementRead(int offset) {
        // DEBUG
        if (offset > oneDirCircularDistance()) {
            PRINTUNIFIEDERROR("Attempting to move read pointer ahead of write pointer!!!\n");
            _Exit(89);
        }

        readIdx = (readIdx + offset + capacity)%capacity;
    }

    inline void incrementWrite(int offset) {
        // DEBUG
        if (offset > (capacity-oneDirCircularDistance()-1)) {
            PRINTUNIFIEDERROR("Attempting to break oneDirCircularDistance invariant!!!\n");
            _Exit(107);
        }

        int newWriteIdx = writeIdx + offset;
        writeIdx = (newWriteIdx+capacity)%capacity;
    }

public:
    explicit RingBuffer(int capacity_ = DEFAULT_CAPACITY) :
            capacity(capacity_), closed(false) {
        ringbuffer = new uint8_t[capacity];
    }

    ~RingBuffer() override {
        delete[] ringbuffer;
    }

    void close() override {
        closed = true;
        // no more writes allowed from now on, just allow emptying read buffer
        ringbuffer_written_cond.notify_one();
    }

    void reset() {
        std::unique_lock<std::mutex> lock(ringbuffer_mutex);
        readIdx = writeIdx = 0;
        PRINTUNIFIEDERROR("RESETTING RINGBUFFER, CURRENT STATUS IS %s",closed?"closed":"open");
        closed = false;
    }

    bool isEmpty() {
        std::unique_lock<std::mutex> lock(ringbuffer_mutex);
        return readIdx == writeIdx;
    }

    ssize_t read(void* buf, size_t count) override {
        std::unique_lock<std::mutex> lock(ringbuffer_mutex);

        // empty buffer, nothing to read, block till some data is available
        // while construct should not be needed here, since we have only one reader thread
        if (readIdx == writeIdx) {
            if (closed) return 0; // EOF-like
//            PRINTUNIFIEDERROR("Underflow, waiting for data to be available...");
            ringbuffer_written_cond.wait(lock);
        }
        // here, we have successfully taken ownership of mutex again
        if (closed && (readIdx == writeIdx)) return 0;// EOF-like FIXME redundant, refactor

        const int maxReadable = oneDirCircularDistance();
        const int bytesToBeCopied = count>maxReadable?maxReadable:count;

        uint8_t* rb = ringbuffer; // C-like data pointer to vector's contiguous memory

        if (writeIdx < readIdx) {
            // split into two memcpys if data chunk crosses the capacity boundary
            // copy from (position from readIdx to capacity)
            int preBoundaryBytes = capacity-readIdx;
            if (bytesToBeCopied > preBoundaryBytes) {
                int postBoundaryBytes = bytesToBeCopied - preBoundaryBytes;
                if (postBoundaryBytes < 0) {
                    PRINTUNIFIEDERROR("postBoundaryBytes: %d\n",postBoundaryBytes);
                    _Exit(109);
                }
                memcpy(buf,rb+readIdx,preBoundaryBytes); // from readIdx to capacity
                memcpy(((uint8_t*)buf)+preBoundaryBytes,rb,postBoundaryBytes); // from 0 to ending position (max writeIdx)
            }
            else {
                memcpy(buf,rb+readIdx,bytesToBeCopied);
            }
        }
        else if (writeIdx == readIdx) { // FIXME sometimes happens (on client close?)
            PRINTUNIFIEDERROR("Ambiguity between empty and full ringbuffer");
            _Exit(119);
        }
        else { // writeIdx > readIdx
            // one copy starting at readIdx
            memcpy(buf,rb+readIdx,bytesToBeCopied);
        }
        incrementRead(bytesToBeCopied);
        ringbuffer_read_cond.notify_one();

        // cout<<"Read: readIdx -> "<<readIdx<<" writeIdx -> "<<writeIdx<<endl;
        return bytesToBeCopied;
    }

    ssize_t write(const void* buf, size_t count) override {
        std::unique_lock<std::mutex> lock(ringbuffer_mutex);

        if (closed) return -1;
        if (oneDirCircularDistance() == capacity-1) {
            // cout<<"Overflow, waiting for data to be read..."<<endl;
            ringbuffer_read_cond.wait(lock);
        }
        if (closed) return -1; // FIXME redundant, refactor

        const int maxWritable = capacity -1 -oneDirCircularDistance();
        if (maxWritable < 0) {
            PRINTUNIFIEDERROR("MaxWritable: %d\n",maxWritable);
            _Exit(103);
        }

        const int bytesToBeCopied = count>maxWritable?maxWritable:count;

        uint8_t* rb = ringbuffer;
        if (readIdx <= writeIdx) {
            // split into two memcpys if data chunk crosses the capacity boundary
            // copy into (position from writeIdx to capacity)
            int preBoundaryBytes = capacity - writeIdx;
            if (preBoundaryBytes < 0) {
                PRINTUNIFIEDERROR("preBoundaryBytes: %d\n",preBoundaryBytes);
                _Exit(101);
            }

            if (bytesToBeCopied > preBoundaryBytes) {
                int postBoundaryBytes = bytesToBeCopied - preBoundaryBytes;
                if (postBoundaryBytes < 0) {
                    PRINTUNIFIEDERROR("postBoundaryBytes: %d\n",postBoundaryBytes);
                    _Exit(97);
                }

                // copy into (position from 0 to read idx -1 !!!)
                memcpy(rb+writeIdx,buf,preBoundaryBytes);
                if (postBoundaryBytes > 0)
                    memcpy(rb,((uint8_t*)buf)+preBoundaryBytes,postBoundaryBytes);
            }
            else {
                memcpy(rb+writeIdx,buf,bytesToBeCopied);
            }
        }
        else {
            // one copy starting at writeIdx
            memcpy(rb+writeIdx,buf,bytesToBeCopied);
        }
        incrementWrite(bytesToBeCopied);
        ringbuffer_written_cond.notify_one();

        // cout<<"Write: readIdx -> "<<readIdx<<" writeIdx -> "<<writeIdx<<endl;
        return bytesToBeCopied;
    }
};

#endif /* RELIABLERINGBUFFER_H */
