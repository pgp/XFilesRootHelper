#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

#include "unifiedlogging.h" 

const char* _ROOTHELPER_VERSION_ = "1.4.8_20200218";

inline void print_roothelper_version() {
	PRINTUNIFIED("Roothelper version %s\n",_ROOTHELPER_VERSION_);
}

#endif /* __ROOTHELPER_VERSION_H__ */
