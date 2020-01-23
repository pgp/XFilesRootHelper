#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

#include "unifiedlogging.h" 

const char* _ROOTHELPER_VERSION_ = "1.4.2pre_20200123";

inline void print_roothelper_version() {
	PRINTUNIFIED("Roothelper version %s\n",_ROOTHELPER_VERSION_);
}

#endif /* __ROOTHELPER_VERSION_H__ */
