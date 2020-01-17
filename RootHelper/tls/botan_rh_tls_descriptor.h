#include "botan_credentials.h"
#include "../ReliableRingBuffer.h"

#ifndef _BOTAN_RH_TLS_DESCRIPTOR_H_
#define _BOTAN_RH_TLS_DESCRIPTOR_H_

class TLSDescriptor : public IDescriptor {
private:
	RingBuffer& inRb;
	Botan::TLS::Channel& channel;
public:
	TLSDescriptor(RingBuffer& inRb_, Botan::TLS::Channel& channel_) : inRb(inRb_), channel(channel_) {}
	
	inline ssize_t read(void* buf, size_t count) override { return inRb.read(buf,count); }
	inline ssize_t readAll(void* buf, size_t count) override { return inRb.readAll(buf,count); }
	inline void readAllOrExit(void* buf, size_t count) override { inRb.readAllOrExit(buf,count); }
	
	// the actual behaviour of these methods is decided by the emit_data callback, which depends on the Botan::TLS::Callbacks implementation used by the Botan::TLS::Server
	inline ssize_t write(const void* buf, size_t count) override { channel.send((uint8_t*)buf,count); return count; } // send return type is void, assume everything has been written
	ssize_t writeAll(const void* buf, size_t count) override { channel.send((uint8_t*)buf,count); return count; }
	void writeAllOrExit(const void* buf, size_t count) override { channel.send((uint8_t*)buf,count); }
	
	void close() override {inRb.close(); channel.close();}
};

#endif /* _BOTAN_RH_TLS_DESCRIPTOR_H_ */
