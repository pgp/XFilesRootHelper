#ifndef _XRE_ANNOUNCER_H_
#define _XRE_ANNOUNCER_H_

#include <iostream>
#include <string>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <chrono>

#define XRE_ANNOUNCE_SERVERPORT 11111

const std::string announce_message = "This is the broadcast message"; // TODO to be populated on start with XRE server IP and port

#ifdef _WIN32
#include "announcer_windows.h"
#else
#include "announcer_unix.h"
#endif

#endif /* _XRE_ANNOUNCER_H_ */
