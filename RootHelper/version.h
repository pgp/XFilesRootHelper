#ifndef __ROOTHELPER_VERSION_H__
#define __ROOTHELPER_VERSION_H__

#include <iostream>

const std::string _ROOTHELPER_VERSION_ = "1.0.1_20190213";

inline void print_roothelper_version() {
	std::cout<<"Roothelper version "<<_ROOTHELPER_VERSION_<<std::endl;
}

#endif /* __ROOTHELPER_VERSION_H__ */
