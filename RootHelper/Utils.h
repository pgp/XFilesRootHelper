#ifndef XRE_RHSS_STANDALONE_UTILS_H
#define XRE_RHSS_STANDALONE_UTILS_H

#ifdef _WIN32
#include "common_win.h"
#endif

#include "path_utils.h"
#include "iowrappers_common.h"
#include <unordered_map>
#include <stack>
#include <functional>
#include <algorithm>

#include "common_uds.h"
#include "resps/ls_resp.hpp"
#include "resps/singleStats_resp.h"
#include "resps/folderStats_resp.h"
#include "desc/FileDescriptorFactory.h"
#include "rh_hasher_botan.h"
#include "homePaths.h"
#include "progressHook.h"
#include "cpuquery.h"

#ifndef _WIN32
#include "pwd.h"
#else
#include <direct.h>
#endif

// MSVC
#ifdef _MSC_VER
typedef int32_t mode_t;
#endif

#define TICKS_PER_SECOND 10000000
#define EPOCH_DIFFERENCE 11644473600LL
time_t convertWindowsTimeToUnixTime(long long int input){
    long long int temp;
    temp = input / TICKS_PER_SECOND; //convert from 100ns intervals to seconds;
    temp = temp - EPOCH_DIFFERENCE;  //subtract number of seconds between epochs
    return (time_t) temp;
}



template<typename T>
#ifdef _WIN32
inline std::wstring TO_STR(T s) {
    return std::to_wstring(s);
}
#else
inline std::string TO_STR(T s) {
    return std::to_string(s);
}
#endif

template<typename STR>
// mandatory absolute path as input
int getFileExtension(const STR& filepath, STR& out, bool filenameWithoutExtensionOrExtensionOnly) {
    auto last = filepath.find_last_of(getSystemPathSeparator());
    if (last == STRNAMESPACE::npos)
        return -1;
    out = filepath.substr(last + 1, filepath.length()); // extract filename
    last = out.find_last_of(getExtSeparator());        // extract extension
    if (last == STRNAMESPACE::npos) { // file has no extension
        if(!filenameWithoutExtensionOrExtensionOnly) out = STRNAMESPACE();
        return 0;
    }
    if(filenameWithoutExtensionOrExtensionOnly) // filename without extension
        out = out.substr(0, last);
    else // extension only
        out = out.substr(last + 1);
    return 0;
}

#ifdef _WIN32
int isDirectoryEmpty(const std::wstring& dirname) {
    auto&& it = itf.createIterator(dirname,FULL,false,PLAIN);
    if(!it) return -1;
    return it.next()?0:1;
}
#else
int isDirectoryEmpty(const std::string& dirname) {
    int n = 0;
    DIR *dir = opendir(dirname.c_str());
    if (dir == nullptr) //Not a directory or doesn't exist
        return -1;
    while (readdir(dir) != nullptr) {
        if(++n > 2) // must contain only the entries '.' and '..'
            break;
    }
    closedir(dir);
    if (n <= 2) //Directory Empty
        return 1;
    else
        return 0;
}
#endif

// predeclaration for osGetSize
template<typename STR>
int accumulateTotalFilesAndFoldersCount(const STR& path, folderStats_resp_t& resp);

#ifdef _WIN32
int osStat(const std::wstring& filepath, singleStats_resp_t& st) {
    WIN32_FILE_ATTRIBUTE_DATA file_attr_data{};
    if(GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &file_attr_data)) {
        LARGE_INTEGER file_size{};
        LARGE_INTEGER file_date{};

        // file/dir attribute
        memcpy(st.permissions,"-rwxrwxrwx",10);
        if (file_attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) st.permissions[0] = 'd';

        // size
        file_size.LowPart = file_attr_data.nFileSizeLow;
        file_size.HighPart = file_attr_data.nFileSizeHigh;
        st.size = file_size.QuadPart;

        // date
        file_date.LowPart = file_attr_data.ftLastWriteTime.dwLowDateTime;
        file_date.HighPart = file_attr_data.ftLastWriteTime.dwHighDateTime;
        st.modificationTime = convertWindowsTimeToUnixTime(file_date.QuadPart);

        return 0;
    }
    else return -1;
}

int64_t osGetSize(const std::wstring& filepath, bool getDirTotalSize = false) {
    WIN32_FILE_ATTRIBUTE_DATA file_attr_data{};
    if(GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &file_attr_data)) {
        LARGE_INTEGER file_size{};
        if(file_attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            if(!getDirTotalSize) return 0; // assume dir node itself has empty size
            else { // for hashing + progress
                folderStats_resp_t resp{};
                if(accumulateTotalFilesAndFoldersCount(filepath, resp) != 0) return -1;
                return resp.totalSize;
            }
        }
        // size of regular file
        file_size.LowPart = file_attr_data.nFileSizeLow;
        file_size.HighPart = file_attr_data.nFileSizeHigh;
        int64_t sz = file_size.QuadPart;
        return sz;
    }
    else return 0;
}
#else

std::string getUsernameByUid(uint32_t uid) {
  struct passwd *pwd;
  pwd = getpwuid(uid); // do not free after use! (man getpwuid)
  if (pwd == nullptr) return "";
  return std::string(pwd->pw_name);
}

int osStat(const std::string& filepath, singleStats_resp_t& resp) {
    struct stat osSt{};
    int ret = lstat(filepath.c_str(), &osSt);
    if (ret < 0) return -1;
    resp.size = osSt.st_size;

    resp.owner = getUsernameByUid(osSt.st_uid);
    resp.group = getUsernameByUid(osSt.st_gid);

    resp.creationTime = osSt.st_ctime;
    resp.modificationTime = osSt.st_mtime;
    resp.lastAccessTime = osSt.st_atime;

    getPermissions(filepath, resp.permissions, osSt.st_mode);
    return 0;
}

int64_t osGetSize(const std::string& filepath, bool getDirTotalSize = false) {
    struct stat osSt{};
    if(lstat(filepath.c_str(),&osSt) != 0) return 0;
    if(S_ISDIR(osSt.st_mode)) {
        if(!getDirTotalSize) return 0; // assume dir node itself has empty size
        else { // for hashing + progress
            folderStats_resp_t resp{};
            if(accumulateTotalFilesAndFoldersCount(filepath, resp) != 0) return -1;
            return resp.totalSize;
        }
    }
    return osSt.st_size;
}
#endif


// 0: not exists or access error
// 1: existing file
// 2: existing directory
#ifdef _WIN32
int existsIsFileIsDir_(const std::wstring& filepath, singleStats_resp_t* st = nullptr) {
    if (st) {
        if (osStat(filepath,*st) < 0) return 0;
        return st->permissions[0]=='d'?2:1;
    }
    else {
        DWORD attrs = GetFileAttributesW(filepath.c_str());
        if (attrs == INVALID_FILE_ATTRIBUTES) return 0;
        return (attrs & FILE_ATTRIBUTE_DIRECTORY)?2:1;
    }
}
#else

int existsIsFileIsDir_(const std::string& filepath, singleStats_resp_t* st = nullptr) {
    singleStats_resp_t x{};
    singleStats_resp_t& kk = (st==nullptr)?x:*st;

    int ret = osStat(filepath,kk);
    if (ret < 0) return 0;
    return kk.permissions[0]=='d'?2:1;
}

#endif

#ifdef _WIN32
int assemble_ls_resp_from_filepath(const std::wstring& filepath, const std::wstring& nameOnly, ls_resp_t& responseEntry, bool fullPathAsName=false) {
    WIN32_FILE_ATTRIBUTE_DATA file_attr_data{};
    if(GetFileAttributesExW(filepath.c_str(), GetFileExInfoStandard, &file_attr_data)) {
        LARGE_INTEGER file_size{};
        LARGE_INTEGER file_date{};

        // file/dir attribute
        memcpy(responseEntry.permissions,"-rwxrwxrwx",10);
        if (file_attr_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) responseEntry.permissions[0] = 'd';

        // size
        file_size.LowPart = file_attr_data.nFileSizeLow;
        file_size.HighPart = file_attr_data.nFileSizeHigh;
        responseEntry.size = file_size.QuadPart;

        // date
        file_date.LowPart = file_attr_data.ftLastWriteTime.dwLowDateTime;
        file_date.HighPart = file_attr_data.ftLastWriteTime.dwHighDateTime;
        responseEntry.date = convertWindowsTimeToUnixTime(file_date.QuadPart);

        // name
        responseEntry.filename = fullPathAsName ? wchar_to_UTF8(filepath) : wchar_to_UTF8(nameOnly);

        return 0;
    }
    else return -1;
}
#else
int assemble_ls_resp_from_filepath(const std::string& filepath, const std::string& nameOnly, ls_resp_t& responseEntry, bool fullPathAsName=false) {
    struct stat st{};
    if (lstat(filepath.c_str(), &st) < 0) return -1;
    responseEntry.filename = fullPathAsName ? filepath : nameOnly;
    responseEntry.date = st.st_mtime;                      // last modification time
    getPermissions(filepath, responseEntry.permissions, st.st_mode); // permissions string
    responseEntry.size = st.st_size;
    return 0;
}
#endif

template<typename STR>
STR getFilenameFromFullPath(const STR& fullPath) {
    ssize_t last = fullPath.find_last_of(getSystemPathSeparator());
    return fullPath.substr(last+1);
}

#ifdef _WIN32
std::wstring getParentDir(std::wstring child) {
    if (!(((child[0] >= L'A' && child[0] <= L'Z') || (child[0] >= L'a' && child[0] <= L'z')) && child[1]==L':')) {
        std::wcerr<<L"[getParentDir] invalid child path: "<<child<<std::endl;
        return L"";
    }
    ssize_t last = child.find_last_of(getSystemPathSeparator());
    if (last < 0) return L""; // FIXME ambiguity on windows (PC root path VS error)
    return child.substr(0, last);
}
#else
std::string getParentDir(const std::string& child) {
    auto sep = getSystemPathSeparator();
    if (child == sep) return "";
    ssize_t last = child.find_last_of(sep);
    if (last < 0) return ""; // TODO to be tested (shouldn't give problems where already used anyway, only wrong error code)
    return child.substr(0, last);
}
#endif

// 0: file, 1: folder, FF: end of list
void read_fileitem_sock_t(fileitem_sock_t& fileitem, IDescriptor& desc) {
    desc.readAllOrExit(&(fileitem.flag),sizeof(uint8_t));
    if (fileitem.flag == 0xFF) return;

    fileitem.file = readStringWithLen(desc);

    if (fileitem.flag == 0x00) {
        desc.readAllOrExit(&(fileitem.size),sizeof(uint64_t));
        PRINTUNIFIED("File info: %s size: %" PRIu64 "\n",fileitem.file.c_str(),fileitem.size);
    }
    else {
        PRINTUNIFIED("Dir info: %s\n",fileitem.file.c_str());
    }
}

#ifdef _WIN32

// de-duplicate contiguous slashes and leave one single trailing slash
std::wstring sanitizeSlashesInWindowsPath(const std::wstring& path) {
    std::wstringstream ss;
    const wchar_t* p = path.c_str();
    bool alreadyEncountered = false;
    for(int i=0; i<path.size(); i++) {
        switch(p[i]) {
            case L'\\':
                if(!alreadyEncountered) {
                    alreadyEncountered=true;
                    ss<<p[i];
                }
                break;
            default:
                ss<<p[i];
                alreadyEncountered=false;
                break;
        }
    }
    if(!alreadyEncountered) ss<<L'\\';
    return ss.str();
}

// exist_ok=false fails only if the leaf directory in the path already exists
int mkpath(const std::wstring& s_, mode_t _unused_, bool exist_ok=true) {
    if (s_ == L".") return 0; // current dir and root always exist
    if (s_.size() < 2) return -1; // at least "C:"
    
    auto&& s = sanitizeSlashesInWindowsPath(s_);

    // check if we are trying to create root dirs for drive units
    
    std::wregex driveUnitRegex {LR"([a-zA-Z]:\\*)"};
    std::wsmatch match;
    
    if (std::regex_match(s_,match,driveUnitRegex)) {
        if(existsIsFileIsDir_(s_) == 2) return 0; // _wmkdir would return error when trying to create a root dir
        else return -1;
    }
    const wchar_t* cstr = s.c_str();

    if (!((cstr[0] >= L'a' && cstr[0] <= L'z') || (cstr[0] >= L'A' && cstr[0] <= L'Z'))) {
        std::wcerr<<L"Invalid unit name for path "<<s_<<std::endl;
        return -1;
    }
    if (cstr[1] != L':') return -1; // not allowed for Windows paths

    const auto S = s.size();
    for (int i=3;i<S;i++) {
        if (cstr[i] == L'\\') {
            std::wstring partialPath = s.substr(0,i);
            std::wcout<<"Creating: "<<partialPath<<" ...";
            // if I'm trying to create an intermediate path and I receive EEXIST, I ignore exist_ok
            if (_wmkdir(partialPath.c_str()) < 0 && ((!exist_ok && i==S-1) || errno != EEXIST)) {
                PRINTUNIFIEDERROR("Error of _wmkdir: %d\n",GetLastError());
                return -1;
            }
            std::cout<<"OK"<<std::endl;
        }
    }
    return 0;
}

inline int mkpathCopyPermissionsFromNearestAncestor(const std::wstring& dirPath, bool exist_ok=true) {
    return mkpath(dirPath,-1,exist_ok);
}

#else

int statNearestAncestor(const std::string& path, struct stat *st) {
    std::string currentAncestor = path;
    std::string parent;
    memset(st, 0, sizeof(struct stat));
    int ret = stat(currentAncestor.c_str(), st);

    while (ret < 0) {
        parent = getParentDir(currentAncestor);
        if (parent.empty())
            return -1;
        memset(st, 0, sizeof(struct stat));
        ret = stat(parent.c_str(), st);
        currentAncestor = parent;
    }
    PRINTUNIFIED("nearest ancestor: %s\n", currentAncestor.c_str());
    return ret; // no accessible ancestor found
}

int mkpath(const std::string& s_, mode_t mode = 0755, bool exist_ok=true) {
    if (s_ == "." || s_ == "/") return 0; // current dir and root always exist

    std::string s = s_ + "/"; // just to avoid code duplication after for loop
    const char* cstr = s.c_str();

    if (cstr[0] != '/') return -1; // not allowed for UNIX paths

    const auto S = s.size();
    for (int i=1;i<S;i++) {
        if (cstr[i] == '/') {
            std::string partialPath = s.substr(0,i);
//            std::cout<<"Creating: "<<partialPath<<" ...";
            if (mkdir(partialPath.c_str(),mode) < 0 && ((!exist_ok && i==S-1) || errno != EEXIST)) return -1;
//            std::cout<<"OK"<<std::endl;
        }
    }
    return 0;
}

int mkpathCopyPermissionsFromNearestAncestor(const std::string& dirPath, bool exist_ok=true) {
    singleStats_resp_t str{};
    int ret = existsIsFileIsDir_(dirPath,&str);
    if(str.permissions[0] == 'd' || str.permissions[0] == 'L') { // dir exists (even as a softlink)
        if(!exist_ok) errno = EEXIST;
        return exist_ok?0:-1;
    }
    if (ret == 1) { // pathname already exists and is a file, ERROR
        errno = EEXIST;
        return -1;
    }

    // if not exists, create it
    struct stat st{};
    ret = statNearestAncestor(dirPath, &st);
    if (ret < 0) return ret;
    ret = mkpath(dirPath,st.st_mode,exist_ok);
    return ret;
}

#endif

// for client to server upload: (local) source and (remote) destination are both full paths (both are assembled in caller)

// outDesc == nullptr means server to client upload and no local socket for communicating progress
template<typename STR>
ssize_t OSUploadRegularFileWithProgress(const STR& source, const STR& destination, singleStats_resp_t& fileinfo, IDescriptor* outDesc, IDescriptor& networkDesc, ProgressHook& progressHook) {
    auto&& input = fdfactory.create(source,FileOpenMode::READ);
    if (!input) return -1;

    static constexpr uint8_t fileFlag = 0x00;

    // TODO send also struct stat's mode_t in order to set destination permissions

    // TO BE SENT OVER NETWORK SOCKET: fileOrDir flag, full (destination) pathname and file size
    auto&& dp = TOUNIXPATH(destination);
    uint16_t destLen = dp.size();
    uint64_t thisFileSize = fileinfo.size;
    PRINTUNIFIED("File size for upload is %" PRIu64 "\n",thisFileSize);

    // one single write command to remote socket wrapper
    uint32_t totalRqSize = sizeof(uint8_t)+sizeof(uint16_t)+destLen+sizeof(uint64_t);
    std::vector<uint8_t> buffer(totalRqSize);
    uint8_t* v = &buffer[0];
    memcpy(v,&fileFlag,sizeof(uint8_t));
    memcpy(v+sizeof(uint8_t),&destLen,sizeof(uint16_t));
    memcpy(v+sizeof(uint8_t)+sizeof(uint16_t),dp.c_str(),destLen);
    memcpy(v+sizeof(uint8_t)+sizeof(uint16_t)+destLen,&thisFileSize,sizeof(uint64_t));
    networkDesc.writeAllOrExit(v,totalRqSize);

    if (outDesc) outDesc->writeAllOrExit(&thisFileSize,sizeof(uint64_t));
    PRINTUNIFIED("File size: %" PRIu64 "\n",thisFileSize);

    uint64_t currentProgress = 0;

    // std::vector<uint8_t> buffer(REMOTE_IO_CHUNK_SIZE);
    buffer.resize(REMOTE_IO_CHUNK_SIZE);
    v = &buffer[0]; // reassign pointer in order to account for realloc relocations

    // FIXME duplicated code from downloadRemoteItems, refactor into a function
    /********* quotient + remainder IO loop *********/
    uint64_t quotient = thisFileSize / REMOTE_IO_CHUNK_SIZE;
    uint64_t remainder = thisFileSize % REMOTE_IO_CHUNK_SIZE;

    PRINTUNIFIED("Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);

    for(uint64_t i=0;i<quotient;i++) {
        input.readAllOrExit(v,REMOTE_IO_CHUNK_SIZE);
        networkDesc.writeAllOrExit(v,REMOTE_IO_CHUNK_SIZE);

        // send progress information back to local socket
        currentProgress += REMOTE_IO_CHUNK_SIZE;

        if (outDesc) outDesc->writeAllOrExit(&currentProgress,sizeof(uint64_t));
        progressHook.publishDelta(REMOTE_IO_CHUNK_SIZE);
    }

    if (remainder != 0) {
        input.readAllOrExit(v,remainder);
        networkDesc.writeAllOrExit(v,remainder);

        // send progress information back to local socket
        currentProgress += remainder;
        
        if (outDesc) outDesc->writeAllOrExit(&currentProgress,sizeof(uint64_t));
        progressHook.publishDelta(remainder);
    }
    /********* end quotient + remainder IO loop *********/

    // end-of-progress indicator removed from here, performed in caller unconditionally

    input.close();
    return 0;
}

#ifndef _WIN32
template<typename STR>
ssize_t OSUploadFromFileDescriptorWithProgress(IDescriptor& input, const STR& destination, uint64_t thisFileSize, IDescriptor* outDesc, IDescriptor& networkDesc, ProgressHook& progressHook) {
    static constexpr uint8_t fileFlag = 0x00;

    // TODO send also struct stat's mode_t in order to set destination permissions

    // TO BE SENT OVER NETWORK SOCKET: fileOrDir flag, full (destination) pathname and file size
    auto dp = TOUNIXPATH(destination);
    uint16_t destLen = dp.size();
    PRINTUNIFIED("File size for upload is %" PRIu64 "\n",thisFileSize);

    // one single write command to remote socket wrapper
    uint32_t totalRqSize = sizeof(uint8_t)+sizeof(uint16_t)+destLen+sizeof(uint64_t);
    std::vector<uint8_t> buffer(totalRqSize);
    uint8_t* v = &buffer[0];
    memcpy(v,&fileFlag,sizeof(uint8_t));
    memcpy(v+sizeof(uint8_t),&destLen,sizeof(uint16_t));
    memcpy(v+sizeof(uint8_t)+sizeof(uint16_t),dp.c_str(),destLen);
    memcpy(v+sizeof(uint8_t)+sizeof(uint16_t)+destLen,&thisFileSize,sizeof(uint64_t));
    networkDesc.writeAllOrExit(v,totalRqSize);

    if (outDesc) outDesc->writeAllOrExit(&thisFileSize,sizeof(uint64_t));
    PRINTUNIFIED("File size: %" PRIu64 "\n",thisFileSize);

    uint64_t currentProgress = 0;

    // std::vector<uint8_t> buffer(REMOTE_IO_CHUNK_SIZE);
    buffer.resize(REMOTE_IO_CHUNK_SIZE);
    v = &buffer[0]; // reassign pointer in order to account for realloc relocations

    // FIXME duplicated code from downloadRemoteItems, refactor into a function
    /********* quotient + remainder IO loop *********/
    uint64_t quotient = thisFileSize / REMOTE_IO_CHUNK_SIZE;
    uint64_t remainder = thisFileSize % REMOTE_IO_CHUNK_SIZE;

    PRINTUNIFIED("Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);

    for(uint64_t i=0;i<quotient;i++) {
        input.readAllOrExit(v,REMOTE_IO_CHUNK_SIZE);
        networkDesc.writeAllOrExit(v,REMOTE_IO_CHUNK_SIZE);

        // send progress information back to local socket
        currentProgress += REMOTE_IO_CHUNK_SIZE;

        if (outDesc) outDesc->writeAllOrExit(&currentProgress,sizeof(uint64_t));
        progressHook.publishDelta(REMOTE_IO_CHUNK_SIZE);
    }

    if (remainder != 0) {
        input.readAllOrExit(v,remainder);
        networkDesc.writeAllOrExit(v,remainder);

        // send progress information back to local socket
        currentProgress += remainder;

        if (outDesc) outDesc->writeAllOrExit(&currentProgress,sizeof(uint64_t));
        progressHook.publishDelta(remainder);
    }
    /********* end quotient + remainder IO loop *********/

    // end-of-progress indicator removed from here, performed in caller unconditionally

    input.close();
    return 0;
}
#endif

// LEGACY
// counts total regular files and folders in a directory subtree; special files are not counted
template<typename STR>
sts countTotalFilesAndFoldersIntoMap(const STR& name, std::unordered_map<STR,sts>& m) {
    sts num{}; // tFiles: 0, tFolders: 0

    switch(existsIsFileIsDir_(name)) {
        case 0:
            PRINTUNIFIEDERROR("Unreadable file, skipping...");
            m[name] = num;
            return num; // in any case of access error, do not count the file
        case 1:
            num.tFiles++;
            m[name] = num;
            return num;
        case 2:
            break;
        default:
            throw std::runtime_error("Invalid efd code");
    }

    num.tFolders++;
    
    auto&& it = itf.createIterator(name,FULL,true,PLAIN);
    if(it)
    while (it.next()) {
        sts subNum = countTotalFilesAndFoldersIntoMap(it.getCurrent(),m);
        num.tFiles += subNum.tFiles;
        num.tFolders += subNum.tFolders;
    }

    m[name] = num; // put in the map for current node
    return num; // to be aggregated at parent level
}

// same as previous method, but accumulates total size as well
template<typename STR>
sts_sz countTotalStatsIntoMap(const STR& name, std::unordered_map<STR,sts_sz>& m) {
    sts_sz num{}; // tFiles: 0, tFolders: 0

    switch(existsIsFileIsDir_(name)) {
        case 0:
            PRINTUNIFIEDERROR("Unreadable file, skipping...");
            m[name] = num;
            return num; // in any case of access error, do not count the file
        case 1:
            num.tFiles++;
            num.tSize = osGetSize(name);
            m[name] = num;
            return num;
        case 2:
            break;
        default:
            throw std::runtime_error("Invalid efd code");
    }

    num.tFolders++;
    
    auto&& it = itf.createIterator(name,FULL,true,PLAIN);
    if(it)
    while (it.next()) {
        sts_sz subNum = countTotalStatsIntoMap(it.getCurrent(),m);
        num.tFiles += subNum.tFiles;
        num.tFolders += subNum.tFolders;
        num.tSize += subNum.tSize;
    }

    m[name] = num; // put in the map for current node
    return num; // to be aggregated at parent level
}

#ifdef _WIN32
constexpr DWORD windrivesBufSize = 512;
std::vector<std::wstring> listWinDrives() {
    wchar_t windrivesBuffer[windrivesBufSize]={};
    wchar_t currentWinDrive[5]={};
    int curWDIndex=0;
    std::wstring joinedDrives;
    std::vector<std::wstring> drives;
    DWORD test;
    int ret = GetLogicalDriveStringsW(windrivesBufSize, windrivesBuffer);
    if (ret == 0) {
        std::cerr<<"Unable to list drives, exiting..."<<std::endl;
        _Exit(217);
    }
    else if (ret > windrivesBufSize) {
        std::cerr<<"Unable to list drives (buffer size not enough), exiting..."<<std::endl;
        _Exit(218);
    }
    else {
        joinedDrives = windrivesBuffer;

        // not working, \0 separator
//        tokenize(joinedDrives,drives);

        for (int i=0;i<windrivesBufSize;i++) {
            if (windrivesBuffer[i]==L'\0') {
                if (windrivesBuffer[i-1]==L'\0') break;

                drives.push_back(std::wstring(currentWinDrive));
                memset(currentWinDrive,0,sizeof(currentWinDrive));
                curWDIndex=0;
            }
            else {
                currentWinDrive[curWDIndex++] = windrivesBuffer[i];
            }
        }

        for (int i=0;i<drives.size();i++) {
            int sz1 = drives[i].size()-1;
            if (drives[i][sz1] == L'\\')
                drives[i].resize(sz1); // truncate trailing separator
        }
    }
    return drives;
}
#endif

// TODO collapse in one listDir method after implementing c++11 range iterator in IDirIterator
// (auto it becomes iterator from IDirIterator or iterator from std vector of logical drives in windows

#ifdef _WIN32
void listDir(IDescriptor& inOutDesc) {
    // read input from socket (except first byte - action code - which has already been read by caller, in order to perform action dispatching)

    // client must send paths in posix format (C:\path becomes /C:/path)
    // dirpath is std::string for unix, std::wstring for windows
    auto&& posixdirpath = readStringWithLen(inOutDesc);
    auto&& dirpath = getSystemPathSeparator(); // placeholder, just for deducing template

    if(posixdirpath.empty()) {
        dirpath = currentXREHomePath;
        inOutDesc.writeAllOrExit(&RESPONSE_REDIRECT,sizeof(uint8_t));
        writeStringWithLen(inOutDesc,TOUNIXPATH(currentXREHomePath));
    }
    else dirpath = FROMUNIXPATH(posixdirpath);

    if (posixdirpath == "/") { // list drives
        sendOkResponse(inOutDesc);
        for (auto& drive : listWinDrives()) {
            ls_resp_t responseEntry{};
            responseEntry.filename = wchar_to_UTF8(drive);
            memcpy(responseEntry.permissions,"drwxrwxrwx",sizeof(responseEntry.permissions));

            if (writels_resp(inOutDesc, responseEntry) != 0) {
                PRINTUNIFIEDERROR("Socket IO error\n");
                inOutDesc.close();
                return;
            }

            PRINTUNIFIED("sent: %s\t%s\t%" PRIu64 "\n", responseEntry.filename.c_str(), responseEntry.permissions, responseEntry.size);
        }
    }
    else {
        auto&& it = itf.createIterator(dirpath,FULL,true,PLAIN);
        if(!posixdirpath.empty()) {
            if(!it) {
                sendErrorResponse(inOutDesc);
                return;
            }
            sendOkResponse(inOutDesc);
        }

        while (it.next()) {
            auto&& filepathname = it.getCurrent();
            auto&& nameonly = it.getCurrentFilename();

            // assemble and send response
            ls_resp_t responseEntry{};
            if (assemble_ls_resp_from_filepath(filepathname,nameonly,responseEntry)!=0) {
                PRINTUNIFIEDERROR("Unable to stat file path: %s\n",filepathname);
                continue;
            }

            if (writels_resp(inOutDesc, responseEntry) != 0) {
                PRINTUNIFIEDERROR("Socket IO error\n");
                inOutDesc.close();
                return;
            }

            PRINTUNIFIED("sent: %s\t%s\t%" PRIu64 "\n", responseEntry.filename.c_str(), responseEntry.permissions, responseEntry.size);
        }
    }

    // list termination indicator
    uint16_t filenamelen = 0;
    inOutDesc.writeAllOrExit(&filenamelen, sizeof(uint16_t));
}
#else
void listDir(IDescriptor& inOutDesc) {
    // read input from socket (except first byte - action code - which has already been read by caller, in order to perform action dispatching)

    // client must send paths in posix format (C:\path becomes /C:/path)
    // dirpath is std::string for unix, std::wstring for windows
    auto&& dirpath = FROMUNIXPATH(readStringWithLen(inOutDesc));

    // retrieve current HOME directory if dirpath is empty, then replace dirpath with HOME
    bool redirectHome = false;
    if(dirpath.empty()) {
        // prepare redirect response, send it later
        dirpath = rhss==-1?currentHomePath:TOUNIXPATH(currentXREHomePath);
        redirectHome = true;
    }
    
    if (rhss_checkAccess(dirpath)) {
        PRINTUNIFIEDERROR("Requested directory listing denied (rhss restricted access)\n");
        errno = EPERM;
        sendErrorResponse(inOutDesc);
        return;
    }

    auto&& it = itf.createIterator(dirpath,FULL,true,PLAIN);
    if (!it) {
        sendErrorResponse(inOutDesc);
        return;
    }

    if(redirectHome) {
        // send redirect response
        inOutDesc.writeAllOrExit(&RESPONSE_REDIRECT,sizeof(uint8_t));
        writeStringWithLen(inOutDesc,rhss==-1?currentHomePath:TOUNIXPATH(currentXREHomePath));
    }
    else sendOkResponse(inOutDesc);
    
    while (it.next()) {
        auto&& filepathname = it.getCurrent();
        auto&& nameonly = it.getCurrentFilename();

        // assemble and send response
        ls_resp_t responseEntry{};
        if (assemble_ls_resp_from_filepath(filepathname,nameonly,responseEntry)!=0) {
            PRINTUNIFIEDERROR("Unable to stat file path: %s\n",filepathname.c_str());
            continue;
        }

        if (writels_resp(inOutDesc, responseEntry) != 0) {
            PRINTUNIFIEDERROR("Socket IO error\n");
            inOutDesc.close();
            return;
        }

        PRINTUNIFIED("sent: %s\t%s\t%" PRIu64 "\n", responseEntry.filename.c_str(), responseEntry.permissions, responseEntry.size);
    }

    // list termination indicator
    uint16_t filenamelen = 0;
    inOutDesc.writeAllOrExit(&filenamelen, sizeof(uint16_t));
    // PRINTUNIFIEDERROR("LISTDIR COMPLETED");
}
#endif

// client sends LS request with flags = 2 (010) and ANY input path
// server replies with xre home dir
void retrieveHomePath(IDescriptor& inOutDesc) {
    auto&& inputPath = readStringWithLen(inOutDesc); // just to free input buffer
    if(!inputPath.empty())
        PRINTUNIFIEDERROR("Warning: non-empty input supplied when requesting home dir path, possible use mismatch");
    
    sendOkResponse(inOutDesc);
#ifdef _WIN32
    writeStringWithLen(inOutDesc,TOUNIXPATH(currentXREHomePath));
#else
    auto homepath = rhss==-1?currentHomePath:TOUNIXPATH(currentXREHomePath);
    writeStringWithLen(inOutDesc,homepath);
#endif
}

#ifdef _WIN32
int genericDeleteBasicImpl(const std::wstring& path) {
    // NOT IMPLEMENTED, NEITHER NECESSARY FOR NOW (Win32: XRE_RHSS, remote delete not available by design)
    return -1;
}
#else
// returns 0 on all files deleted, -1 (+errno) on some files not deleted
int genericDeleteBasicImpl(const std::string& path) {
    int ret = 0;
    std::stack<std::pair<std::string,bool>> S; // boolean value is visited flag, to avoid revisiting node in case of any error in children deletion
    S.push(std::make_pair(path,false));
    while(!S.empty()) {
        bool popped = false;
        std::pair<std::string,bool> p = S.top();
        std::string x = p.first;
        int efd = existsIsFileIsDir_(x);
        if (efd == 0) {
            // PRINTUNIFIED("non-existing file: %s\n",x.c_str());
            S.pop(); // remove and ignore if not exists
            popped = true;
        }
        else if (efd == 1) { // is file, perform delete and remove from stack
            // PRINTUNIFIED("regular file: %s\n",x.c_str());
            S.pop();
            popped = true;
            int ret_ = remove(x.c_str()); // delete file
            if (ret_ < 0) ret = ret_;
        }
        else if (efd == 2) { // is directory
            if (isDirectoryEmpty(x)) {
                // PRINTUNIFIED("empty dir: %s\n",x.c_str());
                S.pop();
                popped = true;
                int ret_ = remove(x.c_str()); // delete empty dir
                if (ret_ < 0) ret = ret_;
            }
            else {
                // non-empty directory that was already visited
                // it means some files within cannot be deleted
                // so, remove it from stack anyway
                if (p.second) {
                    // PRINTUNIFIED("non-empty dir, already visited: %s\n",x.c_str());
                    if (!popped) S.pop();
                    continue;
                }

                // PRINTUNIFIED("non-empty dir, first visit: %s\n",x.c_str());

                // leave on stack (set visited flag to true) and add children on top
                S.pop();
                p.second = true;
                S.push(p);

                struct dirent *d;
                DIR *dir = opendir(p.first.c_str());
                while ((d = readdir(dir)) != nullptr) {
                    // exclude current (.) and parent (..) directory
                    if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
                        continue;
                    std::stringstream ss;
                    ss<<x<<"/"<<d->d_name; // path concat
                    // PRINTUNIFIED("+ adding to stack: %s\n",ss.str().c_str());
                    S.push(std::make_pair(ss.str(),false));
                }
                closedir(dir);
            }
        }
    }
    return ret;
}
#endif

// server handling upload request from client (receive content, save files into chosen remote folder -- sent explicitly by client)
// server handles UPLOAD request sent by client, that is: server DOWNLOADS from client

// FIXME rhss_checkAccess not performed here, because depends from listDir access on GUI (thus, unauthorized requests can be crafted)
// server session invokes this function with cl < 0 (no progress communicated on local socket)
void downloadRemoteItems(IDescriptor& rcl, IDescriptor* cl = nullptr) {
    // receive list consisting of:
    // - type flag (file or empty dir)
    // - string-with-length with full destination pathname (where to save file) - length 0 means list end
    // - size of file (present only if regular file)
    // - file content (present only if regular file)

    // BEGIN moved into downloadRemoteItems

    // receive total number of files to be received
    uint64_t totalFiles,totalSize;
    rcl.readAllOrExit(&totalFiles,sizeof(uint64_t));
    // NEW receive total size as well
    rcl.readAllOrExit(&totalSize,sizeof(uint64_t));


    // propagate total files to local socket
    if(cl) cl->writeAllOrExit(&totalFiles,sizeof(uint64_t));
    // NEW propagate total size as well
    if(cl) cl->writeAllOrExit(&totalSize,sizeof(uint64_t));

    // END moved into downloadRemoteItems

    auto&& progressHook = getProgressHook(totalSize);

    std::vector<uint8_t> buffer(REMOTE_IO_CHUNK_SIZE);

    for(;;) {
        // on each item received, after saving it on the given path, send back ok or error byte (optional)
        fileitem_sock_t fileitem{};
        read_fileitem_sock_t(fileitem,rcl);
        if (fileitem.flag == 0xFF) break;

        auto filepath = FROMUNIXPATH(fileitem.file);

        // open file for writing
        if (fileitem.flag == 0x00) {
            auto parent = getParentDir(filepath);
            if (mkpathCopyPermissionsFromNearestAncestor(parent) < 0) {
                std::string err_str = "Exiting thread, cannot create parent directory " + TOUTF(parent) + " for file " + fileitem.file;
                perror(err_str.c_str());
                rcl.close();
                threadExit();
            }

            auto&& fd = fdfactory.create(filepath,FileOpenMode::XCL);
            if (!fd) {
                std::string err_str = "Exiting thread, cannot open output file " + fileitem.file + " for writing";
                perror(err_str.c_str());
                rcl.close();
                threadExit();
            }

            uint64_t currentProgress = 0;

            // propagate size to local socket
            if(cl) cl->writeAllOrExit(&(fileitem.size),sizeof(uint64_t));
            PRINTUNIFIED("Fileitem size: %" PRIu64 "\n",fileitem.size);

            /********* quotient + remainder IO loop *********/
            uint64_t quotient = fileitem.size / REMOTE_IO_CHUNK_SIZE;
            uint64_t remainder = fileitem.size % REMOTE_IO_CHUNK_SIZE;

            PRINTUNIFIED("Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);

            for(uint64_t i=0;i<quotient;i++) {
                rcl.readAllOrExit(&buffer[0],REMOTE_IO_CHUNK_SIZE);
                fd.writeAllOrExit(&buffer[0],REMOTE_IO_CHUNK_SIZE);

                currentProgress += REMOTE_IO_CHUNK_SIZE;
                if(cl) cl->writeAllOrExit(&currentProgress,sizeof(uint64_t));
                progressHook.publishDelta(REMOTE_IO_CHUNK_SIZE);
            }

            if (remainder != 0) {
                rcl.readAllOrExit(&buffer[0],remainder);
                fd.writeAllOrExit(&buffer[0],remainder);

                currentProgress += remainder;
                if(cl) cl->writeAllOrExit(&currentProgress,sizeof(uint64_t));
                progressHook.publishDelta(remainder);
            }
            /********* end quotient + remainder IO loop *********/

            // end of progress
            if (cl) cl->writeAllOrExit(&maxuint,sizeof(uint64_t));
            PRINTUNIFIED("End of file\n");

            fd.close();
        }
        else if (fileitem.flag == 0x01) {
            mkpathCopyPermissionsFromNearestAncestor(filepath); // TODO propagate return value
        }
        else {
            PRINTUNIFIEDERROR("Unexpected file item flag, exiting thread...");
            rcl.close();
            threadExit();
        }
    }
    
    if (cl) cl->writeAllOrExit(&maxuint_2,sizeof(uint64_t));
    PRINTUNIFIED("End of files\n");
}

// BEGIN NEW *******************************************************

std::vector<std::pair<std::string,std::string>> receivePathPairsList(IDescriptor& desc) {
    std::vector<std::pair<std::string,std::string>> v;
    for(;;) {
        std::vector<std::string> f = readPairOfStringsWithPairOfLens(desc);
        if (f[0].empty()) break;
        v.emplace_back(f[0],f[1]);
    }
    return v;
}

void sendPathPairsList(std::vector<std::pair<std::string,std::string>>& pathPairs, IDescriptor& desc) {
    std::vector<uint8_t> buffer;

    for (auto& item : pathPairs) {
        //~ uint16_t szs[2]{item.first.size(),item.second.size()}; // warning: narrowing conversion
        uint16_t szs[2]{};
        szs[0] = item.first.size();
        szs[1] = item.second.size();

        uint32_t totalRqSize = 2*sizeof(uint16_t)+szs[0]+szs[1];
        buffer.resize(totalRqSize);
        uint8_t* v = &buffer[0];
        memcpy(v,&szs[0],sizeof(uint16_t));
        memcpy(v+sizeof(uint16_t),&szs[1],sizeof(uint16_t));
        memcpy(v+sizeof(uint16_t)+sizeof(uint16_t),item.first.c_str(),szs[0]);
        memcpy(v+sizeof(uint16_t)+sizeof(uint16_t)+szs[0],item.second.c_str(),szs[1]);
        desc.writeAllOrExit(v,totalRqSize);
    }
    uint32_t endOfList = 0;
    desc.writeAllOrExit(&endOfList,sizeof(uint32_t));
}
// END NEW *******************************************************

/*
 * Update: interaction between outer and inner progress
 * Inner progress: always explicit (progress size)
 * Special sizes: 2^64 - x  -> 2^64 -1, 2^64 -2, ...
 * 					-1: EOF (successfully copied a non-directory file) - implicitly increments outer progress by 1
 * 					-2: End Of Transfer (all items copied - with or without error, nothing more can be done by client), sends OK or ERR+errno after
 */
template<typename STR>
int genericUploadBasicRecursiveImplWithProgress(const STR& src_path, // local path (in remote client's filesystem)
                                                const STR& dest_path, // remote path (on rh remote server's filesystem)
                                                IDescriptor& networkDesc,
                                                ProgressHook& progressHook,
                                                IDescriptor* outDesc = nullptr) {
    int ret = 0;
    int ret_ = 0;
    singleStats_resp_t st_x{};

    int efd = existsIsFileIsDir_(src_path,&st_x);
    if (efd <= 0) {
        PRINTUNIFIED("non-existing or non-accessible source file: %s\n", TOUNIXPATH(src_path).c_str());
        return -1;
    }

    // remote flags:
    /*
     * FILE: 0x00
     * DIR: 0x01
     * END OF FILES: 0xFF
     */
    switch (efd) {
        case 1: // file
        {
            PRINTUNIFIED("regular file: %s\tdestination file: %s\n", TOUNIXPATH(src_path).c_str(),TOUNIXPATH(dest_path).c_str());
            ret_ = OSUploadRegularFileWithProgress(src_path,dest_path,st_x,outDesc,networkDesc,progressHook);

            // send EOF (-1) regardless if the file was copied with or without errors
            if (outDesc) outDesc->writeAllOrExit(&maxuint,sizeof(uint64_t));
            PRINTUNIFIED("End of file in caller\n");
            if (ret_ != 0) ret = ret_;
        } // bracket block needed to avoid error due to initialization of local variables and jump to case label
            break;
        case 2: // dir
        {
            bool empty = true;
            auto&& it = itf.createIterator(src_path,FULL,true,PLAIN);
            if(it)
            while (it.next()) {
                empty = false;
                STR&& srcSubDir = pathConcat(src_path,it.getCurrentFilename());
                STR&& destSubDir = pathConcat(dest_path,it.getCurrentFilename());

                // recursive upload non-empty directory
                ret_ = genericUploadBasicRecursiveImplWithProgress(srcSubDir,destSubDir,networkDesc,progressHook,outDesc);
                if (ret_ < 0) ret = ret_;
            }
            if(empty) { // treat also non-listable folders as empty ones
                // send empty dir item to network descriptor
                constexpr uint8_t d = 0x01; // flag byte for folder
                auto&& dp = TOUNIXPATH(dest_path);
                uint16_t y_len = dp.size();

                uint32_t vlen = sizeof(uint8_t)+sizeof(uint16_t)+y_len;
                std::vector<uint8_t> opt_rq(vlen);
                uint8_t* v = &opt_rq[0];
                memcpy(v,&d,sizeof(uint8_t));
                memcpy(v+sizeof(uint8_t),&y_len,sizeof(uint16_t));
                memcpy(v+sizeof(uint8_t)+sizeof(uint16_t),dp.c_str(),y_len);
                networkDesc.writeAllOrExit(v,vlen);
            }
        }
    }

    return ret;
}

// server handles DOWNLOAD request sent by client, that is: server UPLOADS to client
// FIXME rhss_checkAccess not performed here, because depends from listDir access on GUI (unauthorized requests can be crafted though)
template<typename STR>
void server_download(IDescriptor& rcl, const STR& strType) {
    // receive list of pathnames (files and maybe-not-empty folders)
    std::vector<std::pair<std::string,std::string>> v = receivePathPairsList(rcl);

    // count all files in the selection

//    std::unordered_map<STR,sts> descendantCountMap; // LEGACY
    std::unordered_map<STR,sts_sz> descendantCountMap;

//    sts counts{}; // no need to put this in the map // LEGACY
    sts_sz counts{}; // no need to put this in the map

    for (auto& item : v) {
//        sts itemTotals = countTotalFilesAndFoldersIntoMap(FROMUNIXPATH(item.first),descendantCountMap); // LEGACY
        sts_sz itemTotals = countTotalStatsIntoMap(FROMUNIXPATH(item.first),descendantCountMap);
        counts.tFiles += itemTotals.tFiles;
        counts.tFolders += itemTotals.tFolders;
        counts.tSize += itemTotals.tSize;
    }

    auto&& progressHook = getProgressHook(counts.tSize);

    // send counts.tFiles to remote descriptor
    rcl.writeAllOrExit(&(counts.tFiles),sizeof(uint64_t));
    rcl.writeAllOrExit(&(counts.tSize),sizeof(uint64_t));

    for (auto& item : v)
        genericUploadBasicRecursiveImplWithProgress(FROMUNIXPATH(item.first),FROMUNIXPATH(item.second),rcl,progressHook,nullptr);

    // send end of list to remote descriptor
    static constexpr uint8_t endOfList = 0xFF;
    rcl.writeAllOrExit(&endOfList,sizeof(uint8_t));
}

void hashFile(IDescriptor& inOutDesc) {
    uint8_t algorithm;
    uint8_t dirHashOpts; // mandatory for all hash requests, but used only for dir hashing
    inOutDesc.readAllOrExit(&algorithm, sizeof(uint8_t));
    inOutDesc.readAllOrExit(&dirHashOpts, sizeof(uint8_t));
    PRINTUNIFIED("received algorithm hash position is:\t%u\n", algorithm);
    if (algorithm >= rh_hash_maxAlgoIndex) {
        sendErrorResponse(inOutDesc);
        return;
    }

    auto&& filepath = FROMUNIXPATH(readStringWithLen(inOutDesc));
    // PRINTUNIFIED("received filepath to hash is:\t%s\n", filepath.c_str());

    std::vector<uint8_t> digest = rh_computeHash_wrapper(filepath, algorithm, dirHashOpts);

    if (digest.empty()) {
        PRINTUNIFIEDERROR("Size is 0");
        sendErrorResponse(inOutDesc);
        return;
    }

    sendOkResponse(inOutDesc);
    inOutDesc.writeAllOrExit(&digest[0], digest.size());
}

typedef struct {
    std::string hash;
    int ret = 0;
} retHash;

template<typename STR>
retHash createRandomFile(const STR& path, uint64_t size, const std::string& seed, std::string& output_hash, const std::string& backendCipher, IDescriptor* inOutDesc = nullptr) {
    retHash ret;
    bool isLocalSocketProgress = inOutDesc != nullptr;
    size_t written=0,consumed=0;
    constexpr unsigned halfblockSize = HASH_BLOCK_SIZE/2;
    const unsigned keySize = backendCipher == "AES-128/CTR" ? 16 : 32;
    std::vector<uint8_t> inout(HASH_BLOCK_SIZE,0);

    uint8_t* p1 = &inout[0];
    uint8_t* p2 = p1+halfblockSize;

    botan_hash_t hash_out{};
    size_t output_hash_size = 0;
    bool do_output_hash = !output_hash.empty();

    std::vector<uint8_t> outHash_u8;
    if(do_output_hash) {
        output_hash = toUpperCase(output_hash); // allowed values are in cli_hashLabels
        int idx = vecIndexOf(cli_hashLabels, output_hash);
        output_hash = rh_hashLabels[idx]; // convert to Botan allowed value, exception on not found
        output_hash_size = rh_hashSizes[idx];
        botan_hash_init(&hash_out,output_hash.c_str(),0);
        outHash_u8.resize(output_hash_size);
    }

    if(seed.empty()) {
        botan_rng_t rng{};
        botan_rng_init(&rng, nullptr);
        botan_rng_get(rng,p1,keySize);
        botan_rng_destroy(rng);
    }
    else {
        botan_hash_t hash1{};
        botan_hash_init(&hash1,"SHA-256",0);
        botan_hash_update(hash1,(uint8_t*)(seed.c_str()),seed.size());
        botan_hash_final(hash1,p1);
    }

    // expansion
    botan_cipher_t enc{};
#ifdef __aarch64__
    const char* defaultBackendCipher = "ChaCha";
#else
    const char* defaultBackendCipher = has_aes_hw_instructions() ? "AES-256/CTR" : "ChaCha";
#endif
    const char* usedCipher = backendCipher.empty() ? defaultBackendCipher : backendCipher.c_str();
    botan_cipher_init(&enc, usedCipher, Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "AES-256/CTR", Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "ChaCha", Botan::ENCRYPTION);
    // botan_cipher_init(&enc, "SHACAL2/CTR", Botan::ENCRYPTION);
    PRINTUNIFIED("Using %s as underlying cipher for PRNG\n", usedCipher);

    auto&& fd = fdfactory.create(path,FileOpenMode::XCL);
    if(!fd) {
        if(isLocalSocketProgress) sendErrorResponse(*inOutDesc);
        ret.ret = fd.error;
        return ret;
    }
    if(isLocalSocketProgress) sendOkResponse(*inOutDesc);

    auto progressHook = isLocalSocketProgress ? getLSProgressHookP(size, *inOutDesc) : getProgressHookP(size);

    /********* quotient + remainder IO loop *********/
    uint64_t quotient = size / HASH_BLOCK_SIZE;
    uint64_t remainder = size % HASH_BLOCK_SIZE;

    PRINTUNIFIED("[Create random file] Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);

    for(uint64_t i=0;i<quotient;i++) {
        botan_cipher_set_key(enc, p1, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p2, halfblockSize, &written, p1, halfblockSize, &consumed);

        botan_cipher_set_key(enc, p2, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p1, halfblockSize, &written, p2, halfblockSize, &consumed);

        fd.writeAllOrExit(p1,HASH_BLOCK_SIZE);
        if(do_output_hash) botan_hash_update(hash_out,p1,HASH_BLOCK_SIZE);
        progressHook->publishDelta(HASH_BLOCK_SIZE);
    }

    if (remainder != 0) { // there can be at most one block encrypted with same key
        botan_cipher_set_key(enc, p1, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p2, halfblockSize, &written, p1, halfblockSize, &consumed);

        botan_cipher_set_key(enc, p2, keySize);
        botan_cipher_start(enc, nullptr, 0);
        botan_cipher_update(enc, BOTAN_CIPHER_UPDATE_FLAG_FINAL, p1, halfblockSize, &written, p2, halfblockSize, &consumed);

        fd.writeAllOrExit(p1,remainder);
        if(do_output_hash) botan_hash_update(hash_out,p1,remainder);
        progressHook->publishDelta(remainder);
    }
    /********* end quotient + remainder IO loop *********/
    botan_cipher_destroy(enc);
    fd.close();
    // end-of-progress sent in caller
//    if(progressHook != nullptr) {
//        progressHook->publish(maxuint); // end of progress
//        progressHook->doPublish(); // publish thread terminates upon sentinel value maxuint, without publishing it, see why in progressHook.h
//    }
    if(do_output_hash) {
        botan_hash_final(hash_out,&outHash_u8[0]);
        ret.hash = Botan::hex_encode(outHash_u8);
    }
    return ret;
}

template<typename STR>
retHash createEmptyFile(const STR& path, uint64_t size, IDescriptor* inOutDesc = nullptr) {
    retHash ret;
    bool isLocalSocketProgress = inOutDesc != nullptr;
    auto&& fd = fdfactory.create(path,FileOpenMode::XCL);
    if(!fd) {
        if(isLocalSocketProgress) sendErrorResponse(*inOutDesc);
        ret.ret = fd.error;
        return ret;
    }
    if(isLocalSocketProgress) sendOkResponse(*inOutDesc);

    std::vector<uint8_t> emptyChunk(COPY_CHUNK_SIZE,0);

    if(size != 0) {
        auto progressHook = isLocalSocketProgress ? getLSProgressHookP(size, *inOutDesc) : getProgressHookP(size);
        /********* quotient + remainder IO loop *********/
        uint64_t quotient = size / COPY_CHUNK_SIZE;
        uint64_t remainder = size % COPY_CHUNK_SIZE;

        PRINTUNIFIED("[Create empty file] Chunk info: quotient is %" PRIu64 ", remainder is %" PRIu64 "\n",quotient,remainder);

        for(uint64_t i=0;i<quotient;i++) {
            fd.writeAllOrExit(&emptyChunk[0],COPY_CHUNK_SIZE);
            progressHook->publishDelta(COPY_CHUNK_SIZE);
        }
        if (remainder != 0) {
            fd.writeAllOrExit(&emptyChunk[0],remainder);
            progressHook->publishDelta(remainder);
        }
        /********* end quotient + remainder IO loop *********/
    }

    fd.close();
    // end-of-progress sent in caller
//    if(progressHook != nullptr) {
//        progressHook->publish(maxuint); // end of progress
//        progressHook->doPublish(); // publish thread terminates upon sentinel value maxuint, without publishing it, see why in progressHook.h
//    }
    return ret;
}

void createFileOrDirectory(IDescriptor& inOutDesc, uint8_t flags) {

    // sizeof(mode_t) is 2 bytes on Android's 32 bit ABIs (x86, armv7), not 4 bytes
    // mode_t mode;
    int32_t mode; // read 4 bytes anyway, just let the other methods perform the narrowing conversion

    int ret;
    // advanced options for file creation
    uint8_t creationStrategy;
    uint64_t filesize;

    auto&& filepath = FROMUNIXPATH(readStringWithLen(inOutDesc));

    // read mode (mode_t) - 4 bytes
    inOutDesc.readAllOrExit(&mode, sizeof(int32_t));
    PRINTUNIFIED("Received mode %d\n",mode);

    // check access unconditionally (on local, always succeeds because rhss_currentlyServedDirectory is empty)
    if(rhss_checkAccess(filepath)) {
        PRINTUNIFIEDERROR("Requested file/dir creation denied (rhss restricted access)\n");
        errno = EPERM;
        sendErrorResponse(inOutDesc);
        return;
    }

    // flags: b0 (true: create file, false: create directory)
    if(b0(flags)) {
        if(b1(flags)) { // advanced options for create file
            /*
            creation strategy byte flags:
            000 000 00 fallocate (unused)
            000 000 01 zeros   -> & 1
            000 000 10 random  -> & 2 (elif after & 1)

            000 001 10 random + custom seed ( & 4, && (& 2) )
                  ^
                  |
                  custom seed flag bit

            000 010 10 random + out_hash ( & 8, && (& 2) )
                 ^
                 |
                 out_hash flag bit

            000 100 10 random + backend_cipher ( & 16, && (& 2) )
                ^
                |
                backend_cipher flag bit
            */

            std::string seed;
            std::string output_hash; // use both for storing the hash type requested, and for returning the hash hex string
            std::string backend_cipher;
            PRINTUNIFIED("Creating file with advanced options...");
            // receive one byte with creation strategy: 0: FALLOCATE (fastest), 1: ZEROS, 2: RANDOM (slowest), see above for additional flag bits
            inOutDesc.readAllOrExit(&creationStrategy,sizeof(uint8_t));
            // receive file size
            inOutDesc.readAllOrExit(&filesize,sizeof(uint64_t));
            if(creationStrategy & 1) { // zeros
                auto&& retH = createEmptyFile(filepath,filesize,&inOutDesc);
                errno = retH.ret;
            }
            else if(creationStrategy & 2) { // random
                if(creationStrategy & 4) seed = readStringWithLen(inOutDesc); // receive custom seed for PRNG init
                if(creationStrategy & 8) output_hash = readStringWithLen(inOutDesc); // request output of file hash
                if(creationStrategy & 16) backend_cipher = readStringWithLen(inOutDesc); // use custom backend cipher

                auto&& retH = createRandomFile(filepath,filesize,seed,output_hash,backend_cipher,&inOutDesc);
                errno = retH.ret;
                output_hash = retH.hash;
            }
            else {
                PRINTUNIFIEDERROR("Invalid creation strategy for file\n");
                errno = EINVAL;
            }

            inOutDesc.writeAllOrExit(&maxuint,sizeof(uint64_t));
            if((creationStrategy & 2) && (creationStrategy & 8)) writeStringWithLen(inOutDesc, output_hash);
            return;
        }
        else {
            PRINTUNIFIED("Creating file...");
            // mkpath on parent
            auto parent = getParentDir(filepath);

            ret = mkpath(parent, mode);
            if(ret != 0) {
                PRINTUNIFIEDERROR("Unable to mkdir for parent before creating file\n");
                sendErrorResponse(inOutDesc); return;
            }

            // create file
            PRINTUNIFIEDERROR("creating %s after parent dir\n",filepath.c_str());
            auto&& fd = fdfactory.create(filepath,FileOpenMode::XCL);
            if(!fd) ret = -1;
            else {
                fd.close();
                ret = 0;
            }
            sendBaseResponse(ret, inOutDesc);
            return;
        }
    }
    else {
        PRINTUNIFIED("creating directory...\n");
        // mkpath on filepath
        ret = mkpath(filepath, mode, false);
        sendBaseResponse(ret, inOutDesc);
        return;
    }
}

void createHardOrSoftLink(IDescriptor& inOutDesc, uint8_t flags) {
    int ret = 0;
    std::vector<std::string> srcDestPaths = readPairOfStringsWithPairOfLens(inOutDesc);
    auto origin_ = FROMUNIXPATH(srcDestPaths[0]);
    auto link_ = FROMUNIXPATH(srcDestPaths[1]);
    PRINTUNIFIED("Creating %s, target: %s destination: %s\n",b1(flags)?"link":"symlink",origin_.c_str(),link_.c_str());
#ifdef _WIN32
    if (b1(flags)) {
        ret = CreateHardLinkW(link_.c_str(),origin_.c_str(),nullptr) != 0;
    }
    else {
        if(existsIsFileIsDir_(origin_) == 1)
            ret = CreateSymbolicLinkW(link_.c_str(),origin_.c_str(),0x0) != 0; // file softlink
        else
            ret = CreateSymbolicLinkW(link_.c_str(),origin_.c_str(),0x1) != 0; // dir softlink
    }
    if(ret) errno = GetLastError();
#else
    // flags: 010: hard link, 000: soft link
    auto linkfn = b1(flags)?link:symlink;
    ret = linkfn(origin_.c_str(),link_.c_str());
#endif

    sendBaseResponse(ret, inOutDesc);
}

template<typename STR>
int accumulateChildrenFilesAndFoldersCount(const STR& path, folderStats_resp_t& resp) {
    int ret = 0;
    auto&& dirIt = itf.createIterator(path, FULL, false, PLAIN);
    if(dirIt)
    while (dirIt.next()) {
        int efd = existsIsFileIsDir_(dirIt.getCurrent());

        if (efd == 0) {
            ret = -1;
            continue; // ignore this file on read error
        }

        if (efd == 2) resp.childrenDirs++;
        else resp.childrenFiles++;
    }

    return ret;
}

template<typename STR>
int accumulateTotalFilesAndFoldersCount(const STR& path, folderStats_resp_t& resp) {
    int ret = 0;
    auto&& dirIt = itf.createIterator(path, FULL, false);
    if(dirIt)
    while (dirIt.next()) {
        singleStats_resp_t st{};
        int efd = existsIsFileIsDir_(dirIt.getCurrent(),&st);

        if (efd == 0) {
            ret = -1;
            continue; // ignore this file on read error
        }

        if (efd == 2) resp.totalDirs++;
        else {
            resp.totalFiles++;
            resp.totalSize += st.size; // don't accumulate dir entry sizes
        }
    }

    return ret;
}

void stats_file(IDescriptor& inOutDesc) {
    auto&& filepath = FROMUNIXPATH(readStringWithLen(inOutDesc));
    
    // check access unconditionally (on local, always succeeds because rhss_currentlyServedDirectory is empty)
    if (rhss_checkAccess(filepath)) {
        PRINTUNIFIEDERROR("Requested file stat denied (rhss restricted access)\n");
        errno = EPERM;
        sendErrorResponse(inOutDesc);
        return;
    }

    singleStats_resp_t resp{};
    int ret = osStat(filepath,resp);

    if (ret != 0) {
        sendErrorResponse(inOutDesc);
        return; // no stat result if fails on single file
    }

    sendOkResponse(inOutDesc);
    writesingleStats_resp(inOutDesc, resp);
}

void stats_dir(IDescriptor& inOutDesc) {
    auto&& dirpath = FROMUNIXPATH(readStringWithLen(inOutDesc));
    // check access unconditionally (on local, always succeeds because rhss_currentlyServedDirectory is empty)
    if (rhss_checkAccess(dirpath)) {
        PRINTUNIFIEDERROR("Requested dir stat denied (rhss restricted access)\n");
        errno = EPERM;
        sendErrorResponse(inOutDesc);
        return;
    }

    int ret = 0;
    folderStats_resp_t resp{};

    if (accumulateChildrenFilesAndFoldersCount(dirpath, resp) != 0) ret = -1;
    if (accumulateTotalFilesAndFoldersCount(dirpath, resp) != 0) ret = -1;

    sendBaseResponse(ret,inOutDesc); // error means some entries in the directory could not be stat
    writefolderStats_resp(inOutDesc,resp);
}

void stats_multiple(IDescriptor& inOutDesc) {
    int ret = 0;
    folderStats_resp_t resp{};
    
    // receive list of paths
    for(;;) {
        auto&& dirpath = FROMUNIXPATH(readStringWithLen(inOutDesc));
        if (dirpath.empty()) break;
        int efd = existsIsFileIsDir_(dirpath);
        if (efd == 1) { // file
            resp.childrenFiles++;
            resp.totalFiles++;
            resp.totalSize += osGetSize(dirpath);
        }
        else if (efd == 2) { // dir
            if (accumulateChildrenFilesAndFoldersCount(dirpath, resp) != 0) ret = -1;
            if (accumulateTotalFilesAndFoldersCount(dirpath, resp) != 0) ret = -1;
        }
        else { // something listable in parent folder but not stattable (at least in POSIX)
            ret = -1;
        }
    }
    
    sendBaseResponse(ret,inOutDesc); // error means some entries could not be stat
    writefolderStats_resp(inOutDesc,resp);
}

void stats(IDescriptor& inOutDesc, uint8_t flags) {

    if (b0(flags)) { // file stats
        stats_file(inOutDesc);
        return;
    }
    if (b1(flags)) { // dir stats
        stats_dir(inOutDesc);
        return;
    }
    if (b2(flags)) { // multi stats
        // read one file/dir at once, accumulate stats
        stats_multiple(inOutDesc);
        return;
    }
}

#endif //XRE_RHSS_STANDALONE_UTILS_H
