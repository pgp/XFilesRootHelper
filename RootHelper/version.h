#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

#include "unifiedlogging.h"

const char* _ROOTHELPER_VERSION_ = "1.4.9_20200225";

inline void print_roothelper_version() {
	PRINTUNIFIED("Roothelper version %s\n",_ROOTHELPER_VERSION_);
}

#endif /* __ROOTHELPER_VERSION_H__ */
