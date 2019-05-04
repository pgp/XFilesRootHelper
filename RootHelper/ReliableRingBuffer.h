#ifndef RELIABLERINGBUFFER_H
#define RELIABLERINGBUFFER_H

#include <thread>
#include <mutex>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>

#include "unifiedlogging.h"
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
            exit(7256);
        }

        readIdx = (readIdx + offset + capacity)%capacity;
    }

    inline void incrementWrite(int offset) {
        // DEBUG
        if (offset > (capacity-oneDirCircularDistance()-1)) {
            PRINTUNIFIEDERROR("Attempting to break oneDirCircularDistance invariant!!!\n");
            exit(7257);
        }

        int newWriteIdx = writeIdx + offset;
        writeIdx = (newWriteIdx+capacity)%capacity;
    }

public:
    explicit RingBuffer(int capacity_ = DEFAULT_CAPACITY) :
            capacity(capacity_), closed(false) {
        ringbuffer = new uint8_t[capacity];
    }

    ~RingBuffer() {
        delete[] ringbuffer;
    }

    void close() {
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

    ssize_t read(void* buf, size_t count) {
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
                    exit(4693);
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
            exit(-1);
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

    ssize_t write(const void* buf, size_t count) {
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
            exit(3571);
        }

        const int bytesToBeCopied = count>maxWritable?maxWritable:count;

        uint8_t* rb = ringbuffer;
        if (readIdx <= writeIdx) {
            // split into two memcpys if data chunk crosses the capacity boundary
            // copy into (position from writeIdx to capacity)
            int preBoundaryBytes = capacity - writeIdx;
            if (preBoundaryBytes < 0) {
                PRINTUNIFIEDERROR("preBoundaryBytes: %d\n",preBoundaryBytes);
                exit(4792);
            }

            if (bytesToBeCopied > preBoundaryBytes) {
                int postBoundaryBytes = bytesToBeCopied - preBoundaryBytes;
                if (postBoundaryBytes < 0) {
                    PRINTUNIFIEDERROR("postBoundaryBytes: %d\n",postBoundaryBytes);
                    exit(4793);
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

    // returns 0 (write not completed)
    ssize_t writeAll(const void* buf_, size_t count) {
        uint8_t* buf = (uint8_t*)buf_;
        size_t alreadyWritten = 0;
        size_t remaining = count;
        for(;;) {
            ssize_t curr = write(buf+alreadyWritten,remaining);
            if (curr <= 0) return curr; // EOF

            remaining -= curr;
            alreadyWritten += curr;

            if (remaining == 0) return count; // all expected bytes written
        }
    }

    ssize_t readAll(void* buf_, size_t count) {
        uint8_t* buf = (uint8_t*)buf_;
        size_t alreadyRead = 0;
        size_t remaining = count;
        for(;;) {
            ssize_t curr = read(buf+alreadyRead,remaining);
            if (curr <= 0) return curr; // EOF

            remaining -= curr;
            alreadyRead += curr;

            if (remaining == 0) return count; // all expected bytes read
        }
    }
    
    void readAllOrExit(void* buf, size_t count) {
		ssize_t count_ = count;
		if (readAll(buf,count) < count_) {
			PRINTUNIFIEDERROR("Exiting thread %ld on ringbuffer read error\n",std::this_thread::get_id());
			threadExit();
		}
	}

	void writeAllOrExit(const void* buf, size_t count) {
		ssize_t count_ = count;
		if (writeAll(buf,count) < count_) {
			PRINTUNIFIEDERROR("Exiting thread %ld on ringbuffer write error\n",std::this_thread::get_id());
			threadExit();
		}
	}
};

#endif /* RELIABLERINGBUFFER_H */
