#include <iostream>
#include <cstdlib>
#include <string>
#include "version.h"

inline bool prog_is_xre(const std::string& arg) { // match *xre* and *XRE*
	return  arg.find("xre") != std::string::npos ||
			arg.find("XRE") != std::string::npos;
}

inline bool mode_is_help(const std::string& arg) {
	return  arg == "--help" ||
			arg == "/?";
}

inline bool mode_is_xre(const std::string& arg) {
	return 	arg == "--xre";
}

void print_help(const char* program_name) {
	// TODO
	// PRINTUNIFIED("Usage: %s <valid_euid> [optional: <socket_name>]\n",args[0]);
	std::cout<<"Hi, this is help\n";
	_Exit(0);
}

int rh_default_uid = 0;
const std::string rh_uds_default_name = "theroothelper";

//~ void print_rh_arguments(int uid=rh_default_uid, std::string name=rh_uds_default_name) {
	//~ std::cout<<"rh args: "<<uid<<"\t"<<name<<"\n";
	//~ _Exit(0);
//~ }
