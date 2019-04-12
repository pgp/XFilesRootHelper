#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

// #include <iostream>
#include "unifiedlogging.h" 

const char* _ROOTHELPER_VERSION_ = "1.0.3_20190412";

inline void print_roothelper_version() {
	// std::cout<<"Roothelper version "<<_ROOTHELPER_VERSION_<<std::endl;
	PRINTUNIFIED("Roothelper version %s\n",_ROOTHELPER_VERSION_);
}

#endif /* __ROOTHELPER_VERSION_H__ */
