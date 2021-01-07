#ifndef _WIN32
#ifndef __UDS_FD_ITERABLE_H_
#define __UDS_FD_ITERABLE_H_

#include "af_unix_utils.h"

template <typename T>
struct FdIterator {
    T fd;
    T& uds_fd;

    FdIterator(T fd_, T& uds_fd_):
            fd(fd_),uds_fd(uds_fd_) {}

    inline bool operator==(FdIterator& otherNode) {
        return otherNode.fd == this->fd;
    }

    inline bool operator!=(FdIterator& otherNode) {
        return otherNode.fd != this->fd;
    }

    FdIterator operator++() {
        fd = recvfd(uds_fd); // TODO recvfd must generalize as well
        return *this;
    }

    inline int operator*() {
        return fd;
    }
};

class FdIterable {
public:
    FdIterator<int> endItem;
    FdIterator<int> current;
    explicit FdIterable(int udsFd): // uds_fd: UDS to receive regular file descriptors over
            endItem(-1,udsFd),
            current(recvfd(udsFd),udsFd) {}

    inline FdIterator<int>& begin() {
        return current;
    }

    inline FdIterator<int>& end() {
        return endItem; // placeholder for invalid file descriptor
    }
};

#endif /* __UDS_FD_ITERABLE_H_ */
#endif
