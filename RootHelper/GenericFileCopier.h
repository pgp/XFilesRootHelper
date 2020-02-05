#ifndef _RH_FILE_COPIER_H_
#define _RH_FILE_COPIER_H_

#ifdef _WIN32
#include "common_win.h"
#elif defined(__linux__)
#include <linux/limits.h>
#include <sys/sendfile.h>
#else // APPLE and BSD
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/uio.h>
#endif

#include "Utils.h"
#include "FileCopierConstants.h"
#include <iostream>
#include <stack>

template<typename STR>
class ConflictOrErrorInfo_ {
    // 0xFF = unset, 0x00 = FILE, 0x01 = DIR (same convention of fileitem_sock_t flag in roothelper)
public:
    ConflictOrErrorInfo_() : srcType(0xFF),destType(0xFF) {}
    STR src;
    STR dest;
    uint8_t srcType;
    uint8_t destType;
};

typedef enum {
    RENAME,COPY,DELETE // UPLOAD and DOWNLOAD to be added if needed for XRE support
} BaseTransferTaskType;

template<typename STR>
class BaseTransferTask_ {
public:
    BaseTransferTask_(const STR& src_,
                     const STR& dest_,
                     BaseTransferTaskType type_) :
                     src(src_), dest(dest_),type(type_) {} 
    STR src;
    STR dest;
    BaseTransferTaskType type; // dest is empty for DELETE type
}

#ifdef _WIN32
using ConflictOrErrorInfo = ConflictOrErrorInfo_<std::wstring>;
using BaseTransferTask = BaseTransferTask_<std::wstring>;
#else
using ConflictOrErrorInfo = ConflictOrErrorInfo_<std::string>;
using BaseTransferTask = BaseTransferTask_<std::string>;
#endif

#ifdef _WIN32
auto renamefn = _wrename;
#else
auto renamefn = rename;
#endif

template<typename STR>
class FileCopier {
private:
    std::stack<BaseTransferTask> S; // stack containing path pairs to be processed
    const BaseTransferTaskType mainTaskType; // COPY or RENAME (aka move)
    IDescriptor& sockfd;
    std::unordered_map<STR,sts_sz> descendantCountMap;

    sts_sz counts{};

    ConflictOrErrorInfo lastCEInfo;
    int permanentDecs[2]{NO_PREV_DEC,NO_PREV_DEC};
    uint64_t currentProgress = 0; // incremented by 1 at regular file's end of copy, (TODO) incremented by descendantCountMap lookup on directory skip
    
    // private methods
    uint8_t waitConflictOrErrorDec(const BaseTransferTask& task, bool isConflict) {
        sockfd.writeAllOrExit(isConflict?&CFL_ind:&ERR_ind,isConflict?sizeof(CFL_ind):sizeof(ERR_ind));
        writeStringWithLen(sockfd,TOUNIXPATH(task.src));
        writeStringWithLen(sockfd,TOUNIXPATH(task.dest));
        
        sockfd.writeAllOrExit(&(lastCEInfo.srcType),sizeof(lastCEInfo.srcType)); // == cflType in case of conflict
        sockfd.writeAllOrExit(&(lastCEInfo.destType),sizeof(lastCEInfo.destType));
        
        uint8_t ret;
        sockfd.read(&ret,1);
        return ret;
    }

    void unsolvableError(const BaseTransferTask& task) {
        std::cout<<"Unsolvable error for paths: "<<TOUNIXPATH(task.src)<<"\t"<<TOUNIXPATH(task.dest)<<std::endl;
        int dec = waitConflictOrErrorDec(task,false);
        if (dec == CD_CANCEL) {
            std::cout<<"User cancelled, exiting..."<<std::endl;
            threadExit();
        }
        else std::cout<<"User chose to continue to next item..."<<std::endl;
    }

    std::vector<std::pair<STR,STR>> listChildrenPairs(const BaseTransferTask& task) {
        std::vector<std::pair<STR,STR>> v;
        auto&& it = itf.createIterator(task.src,FULL,true,PLAIN);
        if(it)
        while (it.next()) {
            v.push_back(std::make_pair(pathConcat(task.src,it.getCurrentFilename()),
                                       pathConcat(task.dest,it.getCurrentFilename())));
        }
        else std::cerr<<"Unable to open directory for listing: "<<task.src<<std::endl;
        return v;
    }

    void pushChildrenPairs(const BaseTransferTask& task) {
        for (auto& pair : listChildrenPairs(task))
            S.push(BaseTransferTask(pair.first,pair.second,COPY));
    }

/**
 * BEWARE: rename src means copy onto a dest path with different name (not actually rename src file)
 * clearly, this method cannot fail (only the copy on the new candidate path can fail)
 */
    void autoRenameSrc(const BaseTransferTask& task) {
        STR destpath = task.dest;
        std::cout<<"[autoRenameSrc] Preparing renamed dest path for "<<TOUNIXPATH(task.src)<<std::endl;
        const STR destParent = getParentDir(destpath);
        const STR destName = getFilenameFromFullPath(destpath);
        unsigned count = 0;
        int efd = existsIsFileIsDir_(destpath);

        while (efd != 0) { // while destpath exists
            destpath = pathConcat(destParent,destName+TO_STR(count));
            efd = existsIsFileIsDir_(destpath);
            count++;
        }
        S.push(BaseTransferTask(task.src,destpath,COPY));
        std::cout<<"Renamed path pair repushed"<<std::endl;
    }

    void pushSrcWithDifferentName(const STR& srcI, const STR& destI, const STR& newname) {
        const STR renamedDestPath = getParentDir(destI) + getSystemPathSeparator() + newname;
        S.push(BaseTransferTask(srcI,renamedDestPath,COPY));
    }

    void sendSkipInfo(const STR& srcI, uint8_t cflType) {
        sockfd.writeAllOrExit(&SKIP_ind,sizeof(uint64_t));
        sts_sz sk = descendantCountMap[srcI];
        sockfd.writeAllOrExit(&(sk.tFiles),sizeof(uint64_t));
        sockfd.writeAllOrExit(&(sk.tSize),sizeof(uint64_t));
    }

/**
 * rename dest == rename task.dest to a non-existing path
 * may fail, actually we have to rename the destination path
 */
    void autoRenameDest(const BaseTransferTask& task) {
        std::cout<<"[autoRenameDest] Attempting rename of dest path "<<TOUNIXPATH(task.dest)<<std::endl;
        STR destpath = task.dest;
        STR old_destpath = destpath;
        const STR destParent = getParentDir(destpath);
        const STR destName = getFilenameFromFullPath(destpath);
        unsigned count = 0;
        int efd = existsIsFileIsDir_(destpath);

        while (efd != 0) {
            destpath = pathConcat(destParent, destName + TO_STR(count));
            efd = existsIsFileIsDir_(destpath);
            count++;
        }

        if (renamefn(old_destpath.c_str(), destpath.c_str()) < 0) {
            unsolvableError(BaseTransferTask(task.src,destpath,COPY)); // or unsolvableError(old_destpath,destpath)
            return;
        }
        S.push(BaseTransferTask(task.src,old_destpath,COPY));
        std::cout<<"[autoRenameDest] Rename of dest path succeeded, path pair repushed"<<std::endl;
    }

    void handleConflict(BaseTransferTask task, int dec, int cflType) {
        STR newname; // name only, received
        STR newpathname; // full path, assembled later

        switch(dec) {
            case CD_OVERWRITE_ALL:
            case CD_SKIP_ALL:
            case CD_REN_SRC_ALL:
            case CD_REN_DEST_ALL:
            case CD_MERGE_ALL:
                permanentDecs[cflType] = dec;
                break;
        }

        switch(dec) {
            case CD_SKIP:
            case CD_SKIP_ALL:
                std::cout<<"Skip or skip all chosen"<<std::endl;
                sendSkipInfo(task.src,cflType);
                return;
            case CD_OVERWRITE:
            case CD_OVERWRITE_ALL:
                std::cout<<"Overwrite or overwrite all chosen"<<std::endl;
                if (genericDeleteBasicImpl(task.dest) < 0) {
                    unsolvableError(BaseTransferTask(task.src,task.dest,COPY)); // on skip ok, or exits directly
                    return;
                }
                else {
                    S.push(BaseTransferTask(task.src,task.dest,COPY)); // repush
                }
                break;
            case CD_REN_DEST_ALL:
                autoRenameDest(BaseTransferTask(task.src,task.dest,COPY));
                break;
            case CD_REN_DEST:
                std::cout<<"[CD_REN_DEST] Attempting rename of destination path"<<std::endl;
                newname = FROMUNIXPATH(readStringWithLen(sockfd)); // newname must be name only
                std::cout<<"[CD_REN_DEST] Received new name is "<<TOUNIXPATH(newname)<<std::endl;
                newpathname = getParentDir(task.dest) + getSystemPathSeparator() + newname;
                if (renamefn(task.dest.c_str(),newpathname.c_str()) < 0) {
                    unsolvableError(BaseTransferTask(task.src,task.dest,COPY)); // on skip ok, or exits directly
                    return;
                }
                else {
                    S.push(BaseTransferTask(task.src,task.dest,COPY)); // repush
                    std::cout<<"[CD_REN_DEST] Repush OK"<<std::endl;
                }
                break;
            case CD_REN_SRC_ALL:
                autoRenameSrc(BaseTransferTask(task.src,task.dest,COPY));
                break;
            case CD_REN_SRC:
                std::cout<<"[CD_REN_SRC] Repushing path pair with destination path with custom name"<<std::endl;
                newname = FROMUNIXPATH(readStringWithLen(sockfd)); // newname must be name only
                std::cout<<"[CD_REN_SRC] Received new name is"<<TOUNIXPATH(newname)<<std::endl;

                pushSrcWithDifferentName(task.src,task.dest,newname);
                std::cout<<"[CD_REN_SRC] Repush OK"<<std::endl;
                break;
            case CD_MERGE:
            case CD_MERGE_ALL:
                pushChildrenPairs(task);
                break;
            case CD_CANCEL:
                std::cout<<"Exiting from handleConflict on user choice"<<std::endl;
                threadExit();
        }
    }

    int fileCopy(const BaseTransferTask& task) {
        int retval;
        int destEfd = existsIsFileIsDir_(task.dest);
        if (destEfd == 0) {
            if (oscopyfile(task.src,task.dest) == UNSOLVABLE_ERROR_TYPE) retval = UNSOLVABLE_ERROR_TYPE;
            else retval = OK_TYPE;
        }
        else { // destination path exists
            if (permanentDecs[CONFLICT_TYPE_FILE] == NO_PREV_DEC) {
                retval = CONFLICT_TYPE_FILE;
            }
            else {
                if (permanentDecs[CONFLICT_TYPE_FILE] == CD_SKIP_ALL) {
                    sendSkipInfo(task.src,CONFLICT_TYPE_FILE);
                }
                else {
                    handleConflict(BaseTransferTask(task.src,task.dest,COPY),permanentDecs[CONFLICT_TYPE_FILE],CONFLICT_TYPE_FILE);
                }
                retval = OK_TYPE;
            }
        }

        if (retval == CONFLICT_TYPE_FILE || retval == UNSOLVABLE_ERROR_TYPE) {
            lastCEInfo.src = task.src;
            lastCEInfo.dest = task.dest;
            lastCEInfo.srcType = 0x00;
            lastCEInfo.destType = (destEfd == 1)?0x00:0x01;
        }
        return retval;
    }

    int dirCopy(const BaseTransferTask& task) {
        int retval;
        int destEfd = existsIsFileIsDir_(task.dest);
        if (destEfd == 0) {
            if (mkpath(task.dest) < 0) {
                retval = UNSOLVABLE_ERROR_TYPE;
            }
            else {
                pushChildrenPairs(task);
                retval = OK_TYPE;
            }
        }
        else { // destination path exists
            if (permanentDecs[CONFLICT_TYPE_DIR] == NO_PREV_DEC) {
                retval = CONFLICT_TYPE_DIR;
            }
            else {
                if (permanentDecs[CONFLICT_TYPE_DIR] == CD_SKIP_ALL) {
                    sendSkipInfo(task.src,CONFLICT_TYPE_DIR);
                }
                else {
                    handleConflict(BaseTransferTask(task.src,task.dest,COPY),permanentDecs[CONFLICT_TYPE_DIR],CONFLICT_TYPE_DIR);
                }
                retval = OK_TYPE;
            }
        }

        if (retval == CONFLICT_TYPE_DIR || retval == UNSOLVABLE_ERROR_TYPE) {
            lastCEInfo.src = task.src;
            lastCEInfo.dest = task.dest;
            lastCEInfo.srcType = 0x01;
            lastCEInfo.destType = (destEfd == 1)?0x00:0x01;
        }
        return retval;
    }

#ifdef _WIN32
    ssize_t oscopyfile(const STR& source, const STR& destination) {
        bool sizeSent = false;
        auto progressRoutine = [&](LARGE_INTEGER TotalFileSize,
                              LARGE_INTEGER TotalBytesTransferred,
                              LARGE_INTEGER StreamSize,
                              LARGE_INTEGER StreamBytesTransferred,
                              DWORD dwStreamNumber,
                              DWORD dwCallbackReason,
                              HANDLE hSourceFile,
                              HANDLE hDestinationFile,
                              LPVOID lpData) -> DWORD {
                              // usable only with IDescriptor (TLSDescriptor)
             if (!sizeSent) {
                  sockfd.writeAllOrExit(&TotalFileSize,sizeof(uint64_t));
                  sizeSent = true;
             }
             else {
                  sockfd.writeAllOrExit(&TotalBytesTransferred,sizeof(uint64_t));
             }
            return 0;
        };
        BOOL ret = CopyFileExW(source.c_str(),destination.c_str(),nullptr,nullptr,0,COPY_FILE_FAIL_IF_EXISTS);
        if(!(ret)) return UNSOLVABLE_ERROR_TYPE;
        sockfd.writeAllOrExit(&EOF_ind,sizeof(uint64_t));
        return OK_TYPE;
    }
#else
    ssize_t oscopyfile(const STR& source, const STR& destination) {
        // in dir copy:
        // first, get total children files (from stats_dir), use that value as number of end progresses to receive
        // then, for each regular file, receive progress realtive to copied bytes
        // the aggregate progress percentage computed client-side will be (currentFileCount/totalFiles)*(currentCopiedBytes/thisFileTotalBytes)*100
        int input, output;
        if ((input = open(source.c_str(), O_RDONLY)) == -1) {
            return UNSOLVABLE_ERROR_TYPE;
        }

        if ((output = open(destination.c_str(), O_RDWR | O_CREAT, 0644)) == -1) {
            close(input);
            return UNSOLVABLE_ERROR_TYPE;
        }

        // sendfile will work with non-socket output (i.e. regular file) on Linux 2.6.33+
        off_t offset = 0;
        struct stat fileinfo{};
        fstat(input, &fileinfo);
        ssize_t chunkWrittenBytes;

        // send total fileinfo.st_size
        uint64_t thisFileSize = fileinfo.st_size;
        sockfd.writeAllOrExit(&thisFileSize,sizeof(uint64_t));
        uint64_t currentProgress = 0;

        for(;;) {
#ifdef __linux__
            chunkWrittenBytes = sendfile(output, input, &offset, COPY_CHUNK_SIZE);
#else
#ifdef __APPLE__
            off_t inExpectedTransferredBytes_outActuallyTransferredBytes = COPY_CHUNK_SIZE;
            sendfile(input, output, offset, &inExpectedTransferredBytes_outActuallyTransferredBytes, nullptr, 0); // TODO check if i/o off_t is absolute of delta
            offset += inExpectedTransferredBytes_outActuallyTransferredBytes; // TODO valid if delta, check
            chunkWrittenBytes = inExpectedTransferredBytes_outActuallyTransferredBytes;
#else // BSD
            off_t actuallyTransferredBytes;
            sendfile(input, output, offset, COPY_CHUNK_SIZE, nullptr, &actuallyTransferredBytes, 0);  // TODO check retval and oltranza mode with param 0 otherwise st_size
            offset += actuallyTransferredBytes; // TODO valid if delta, check
            chunkWrittenBytes = actuallyTransferredBytes;
#endif
#endif
            printf("Current offset: %lu\n",offset);
            if (chunkWrittenBytes <= 0) break;
            // send progress
            currentProgress += chunkWrittenBytes;
            sockfd.writeAllOrExit(&currentProgress,sizeof(uint64_t));
        }

        // end-of-progress indicator removed from here, performed in caller unconditionally
        // UPDATE put here again, changed skip logic
        sockfd.writeAllOrExit(&EOF_ind,sizeof(uint64_t));

        struct stat fst{};
        fstat(input,&fst);
        fchmod(output,fst.st_mode);

        close(input);
        close(output);

        return OK_TYPE;
    }
#endif

    // UNUSED, generic move for regullar files (tries rename, then copy-and-delete-source)
    ssize_t genericRenameFileOnly(const STR& source, const STR& destination) {
#ifdef _WIN32
        // windows (_w)rename works also across devices
        return renamefn(source.c_str(), destination.c_str()); // BUT, no progress can be sent from here
#else
        int ret = renamefn(source.c_str(), destination.c_str());
        if (ret < 0) {
            if (errno != EXDEV) return ret;

            ret = oscopyfile(source,destination);
            if (ret < 0) return ret;
            //~ return genericDeleteBasicImpl(source);
            return remove(source.c_str());
        }
        else return ret;
#endif
    }
    
    void populateStackFromCopyList(std::vector<BaseTransferTask>& copyList) {
        for (auto& x : copyList) {
            // maps
            sts_sz itemTotals = countTotalStatsIntoMap(x.src,descendantCountMap);
            
            counts.tFiles += itemTotals.tFiles;
            counts.tFolders += itemTotals.tFolders;
            
            counts.tSize += itemTotals.tSize;

            S.push(x);
        }
    }
    
public:
    void maincopy() {
        try {
            int errors = 0;
            // send total number of non-directory files to allow client to update outer progress
            sockfd.writeAllOrExit(&(counts.tFiles),sizeof(uint64_t));
            // send total size as well
            sockfd.writeAllOrExit(&(counts.tSize),sizeof(uint64_t));

            while (!S.empty()) {
                auto pair = S.top();
                S.pop();
                
                // TODO switch task type
                
                int efd = existsIsFileIsDir_(pair.src);
                int ret;
                int dec;
                switch(efd) {
                    case 1:
                        ret = fileCopy(pair);
                        break;
                    case 2:
                        ret = dirCopy(pair);
                        break;
                    default:
                        std::cerr<<"Non-existing src file in pair (parent folder content changed?)"<<std::endl;
                        return;
                }
                if (ret == UNSOLVABLE_ERROR_TYPE) {
                    unsolvableError(pair);
                }
                else if (ret == CONFLICT_TYPE_FILE || ret == CONFLICT_TYPE_DIR) {
                    dec = waitConflictOrErrorDec(pair,true);
                    handleConflict(pair,dec,ret);
                }
                else { // copy OK
                    if (efd == 1) {
                        currentProgress++;
                    }
                }
            }

            // end-of-files indication
            sockfd.writeAllOrExit(&EOFs_ind,sizeof(uint64_t));
            sendBaseResponse(errors, sockfd); // TODO 'errors' to be kept updated, or use other variable
        }
        catch(threadExitThrowable& i) {
            std::cout<<"ThreadExit invoked, exiting..."<<std::endl;
        }
        sockfd.close();
    }

    FileCopier(IDescriptor& sockfd_, BaseTransferTaskType mainTaskType_ = COPY) : sockfd(sockfd_), mainTaskType(mainTaskType_) {
        std::vector<BaseTransferTask> copyList;

        // read list of source-destination path pairs
        for(;;) {
            // read file paths
            std::vector<std::string> f = readPairOfStringsWithPairOfLens(sockfd);
            if (f[0].empty()) break;

            PRINTUNIFIED("Received item source path: %s\n",f[0].c_str());
            PRINTUNIFIED("Received item destination path: %s\n",f[1].c_str());
            copyList.push_back(BaseTransferTask(FROMUNIXPATH(f[0]),FROMUNIXPATH(f[1]),COPY));
        }

        populateStackFromCopyList(copyList);
    }
};

#endif /* _RH_FILE_COPIER_H_ */
