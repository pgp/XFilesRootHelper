#ifndef IDESCRIPTOR_H
#define IDESCRIPTOR_H

// both binary
typedef enum {
	READ,
	WRITE
} FileOpenMode;

// TODO *all and *allOrExit methods are common implementations, should be defined here

// interface, only pure virtual functions
class IDescriptor {
public:
	virtual ssize_t read(void* buf, size_t count) = 0;
	virtual ssize_t readAll(void* buf, size_t count) = 0;
	virtual void readAllOrExit(void* buf, size_t count) = 0;
	virtual ssize_t write(const void* buf, size_t count) = 0;
	virtual ssize_t writeAll(const void* buf, size_t count) = 0;
	virtual void writeAllOrExit(const void* buf, size_t count) = 0;
	virtual void close() = 0;
	virtual ~IDescriptor() {};
};

class IDescriptorFactory {
public:
	virtual std::unique_ptr<IDescriptor> createFileDescriptor(std::string file_, FileOpenMode mode_) = 0;
	virtual std::unique_ptr<IDescriptor> createNetworkDescriptor() = 0;
};

#endif /* IDESCRIPTOR_H */
