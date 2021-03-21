#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

#include "unifiedlogging.h"

const char* _ROOTHELPER_VERSION_ = "1.8.5_20210321";

inline void print_roothelper_version() {
	PRINTUNIFIED("Roothelper version %s\n",_ROOTHELPER_VERSION_);
}

#endif /* __ROOTHELPER_VERSION_H__ */
