#ifndef _LS_RESP_H_
#define _LS_RESP_H_

#include <cstdint>
#include <cstring>
#include <dirent.h>

#include "../desc/IDescriptor.h"


typedef struct {
    std::string filename;
    uint32_t date;
    char permissions[10];
    uint64_t size;
} ls_resp_t;

// wrapper
int writels_resp(IDescriptor& fd, const ls_resp_t& dataContainer) {
	uint16_t len = dataContainer.filename.length();
    if (fd.write(&len,sizeof(uint16_t)) < sizeof(uint16_t)) return -1;
    if (fd.write(dataContainer.filename.c_str(),len) < len) return -1;
    if (fd.write(&(dataContainer.date),sizeof(uint32_t)) < sizeof(uint32_t)) return -1;
    if (fd.write(dataContainer.permissions,10) < 10) return -1;
    if (fd.write(&(dataContainer.size),sizeof(uint64_t)) < sizeof(uint64_t)) return -1;
    return 0;
}

inline void writeLsRespOrExit(IDescriptor& fd, const ls_resp_t& dataContainer) {
	if (writels_resp(fd,dataContainer) < 0) {
		PRINTUNIFIEDERROR("IO error while writing ls response entry\n");
		// close(fd); // used in listArchive, really needed? TODO To be tested, commented out, we are not in a forked process in listArchive
		threadExit();
	}
}

#ifndef _WIN32
// TODO avoid string conversion from roothelper, do it from client
// TODO SUID/GUID and special files printing
void getPermissions(const std::string& pathname, char *output, mode_t st_mode) {
    int i=0;
    // output[i] = (S_ISDIR(st_mode)) ? 'd' : '-'; i++; // LEGACY

    if (S_ISLNK(st_mode)) {
        // 'l' for link to file, 'L' for link to folder
        struct stat st{};
        stat(pathname.c_str(),&st); // stat resolves symlinks
        output[i] = (S_ISDIR(st.st_mode)) ? 'L' : 'l'; i++; // non-standard, use non-POSIX 'L' for softlink to folder
    }
    else if (S_ISDIR(st_mode)) output[i] = 'd';
    else output[i] = '-';
    i++;

    output[i] = (st_mode & S_IRUSR) ? 'r' : '-'; i++;
    output[i] = (st_mode & S_IWUSR) ? 'w' : '-'; i++;
    output[i] = (st_mode & S_IXUSR) ? 'x' : '-'; i++;
    output[i] = (st_mode & S_IRGRP) ? 'r' : '-'; i++;
    output[i] = (st_mode & S_IWGRP) ? 'w' : '-'; i++;
    output[i] = (st_mode & S_IXGRP) ? 'x' : '-'; i++;
    output[i] = (st_mode & S_IROTH) ? 'r' : '-'; i++;
    output[i] = (st_mode & S_IWOTH) ? 'w' : '-'; i++;
    output[i] = (st_mode & S_IXOTH) ? 'x' : '-';
}

// TODO replace in permission string builder
//~ char f_type(mode_t mode)
//~ {
//~ char c;

//~ switch (mode & S_IFMT)
//~ {
//~ case S_IFBLK:
//~ c = 'b';
//~ break;
//~ case S_IFCHR:
//~ c = 'c';
//~ break;
//~ case S_IFDIR:
//~ c = 'd';
//~ break;
//~ case S_IFIFO:
//~ c = 'p';
//~ break;
//~ case S_IFLNK:
//~ c = 'l';
//~ break;
//~ case S_IFREG:
//~ c = '-';
//~ break;
//~ case S_IFSOCK:
//~ c = 's';
//~ break;
//~ default:
//~ c = '?';
//~ break;
//~ }
//~ return (c);
//~ }
#endif

#endif /* _LS_RESP_H_ */
