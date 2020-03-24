#include "common_uds.h"

#include <mutex>
#include <unordered_map>
#include <vector>
#include <algorithm>
#include <iterator>
#include <sstream>
#include <stack>
#include <functional>
#include <thread>

#include "tls/botan_rh_tls_descriptor.h"
#include "desc/AutoFlushBufferedWriteDescriptor.h"

#include "tls/botan_rh_rserver.h"
#include "tls/basic_https_client.h"

#include "resps/singleStats_resp.h"
#include "resps/folderStats_resp.h"

#include "FileCopier.h"

#ifndef _WIN32
#include "desc/PosixDescriptor.h"
#include "androidjni/wrapper.h"
#include "archiveUtils.h"
#endif

#include "xre.h"
#include "cli/cli_parser.h"

#define PROGRAM_NAME "roothelper"
char SOCKET_ADDR[32]={};
int CALLER_EUID = -1; // populated by second command-line argument, in order to verify client credentials (euid must match on connect)

bool lib7zLoaded = false;

//////// string IO methods moved in iowrappers.h //////////////

/*******************************************************************
 *******************************************************************
 ********************************************************************/
// Embedding of PGP's roothelper //

inline bool has_suffix(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

////////////// path manipulation and file existence methods moved in fileutils.h //////////////////////

// for special files as well
// st passed as pointer for later use (e.g. in mkpath)
int getFileType_(const char* filepath, struct stat* st) {
    int ret = stat(filepath, st);
    if (ret < 0) return ret; // Not existing on non-accessible path
    switch (st->st_mode & S_IFMT) {
        case S_IFBLK:
            return DT_BLK;
        case S_IFCHR:
            return DT_CHR;
        case S_IFDIR:
            return DT_DIR; // TODO replace in caller
        case S_IFIFO:
            return DT_FIFO;
        case S_IFLNK:
            return DT_LNK;
        case S_IFREG:
            return DT_REG; // TODO replace in caller
        case S_IFSOCK:
            return DT_SOCK;
        default:
            return DT_UNKNOWN; // stattable but unknown
    }
}

void existsIsFileIsDir(IDescriptor& inOutDesc, uint8_t flags) { 
  
  std::string filepath = readStringWithLen(inOutDesc);
  PRINTUNIFIED("received filepath to check for existence is:\t%s\n", filepath.c_str());

  uint8_t respFlags = 0;
  struct stat st;
  if (b0(flags))
  { // b0: check for existence
    PRINTUNIFIED("Checking existence...\n");
    if (access(filepath.c_str(), F_OK) == 0)
      SETb0(respFlags, 1); // set bit 0 if file exists
    else goto response; // all to zero if path not exists
  }
  stat(filepath.c_str(), &st);
  if (b1(flags))
  { // b0: check if is file (false even if not exists)
    PRINTUNIFIED("Checking \"is file\"...\n");
    if (S_ISREG(st.st_mode))
      SETb1(respFlags, 1);
  }
  if (b2(flags))
  { // b0: check if it is folder (false even if not exists)
    PRINTUNIFIED("Checking \"is dir\"...\n");
    if (S_ISDIR(st.st_mode))
      SETb2(respFlags, 1);
  }

// response byte unconditionally 0x00 (OK), plus one byte containing the three bit flags set accordingly:
// b0: true if file or dir exists
// b1: true if is file
// b2: true if is dir
response:
  sendOkResponse(inOutDesc);
  inOutDesc.writeAllOrExit( &respFlags, 1);
}

// for client to server upload: (local) source and (remote) destination are both full paths (both are assembled in caller)

int renamePathMakeAncestors(std::string oldPath, std::string newPath) {
	std::string parent = getParentDir(newPath);
	int efd_parent = existsIsFileIsDir_(parent);
		switch(efd_parent) {
			case 0: // not existing
				if (mkpathCopyPermissionsFromNearestAncestor(parent) != 0)
					return -1;
				break;
			case 1: // existing file
				errno = EEXIST;
				return -1;
			case 2: // existing directory
				break;
			default:
				errno = EINVAL;
				return -1;
		}
	return rename(oldPath.c_str(), newPath.c_str());
}

// Does not attempt to copy-then-delete, nor to merge folders on move
// only creates ancestor paths if they don't exist
void moveFileOrDirectory(IDescriptor& inOutDesc)
{
	std::vector<std::string> v_fx;
	std::vector<std::string> v_fy;
  
	// read list of source-destination path pairs
	// BEGIN RECEIVE DATA
	for(;;) {		
		std::vector<std::string> f = readPairOfStringsWithPairOfLens(inOutDesc);
		PRINTUNIFIED("Received item source path: %s\n",f[0].c_str());
		PRINTUNIFIED("Received item destination path: %s\n",f[1].c_str());
		if (f[0].empty()) break;
	
		v_fx.push_back(f[0]);
		v_fy.push_back(f[1]);
	}
	// END RECEIVE DATA
	
	int ret = 0;
	
	for (uint32_t i = 0; i<v_fx.size(); i++) {
		if (renamePathMakeAncestors(v_fx[i],v_fy[i]) < 0) {
			ret = -1;
			break;
		}
		
		inOutDesc.writeAllOrExit(&maxuint,sizeof(uint64_t)); // EOF progress
	}
	
	inOutDesc.writeAllOrExit(&maxuint_2,sizeof(uint64_t)); // EOFs progress
	// @@@
	if (ret == 0)
	PRINTUNIFIEDERROR("@@@MOVE SEEMS OK");
	else
	PRINTUNIFIEDERROR("@@@MOVE ERROR, errno is %d",errno);
	// @@@
	sendBaseResponse(ret,inOutDesc);
}

void copyFileOrDirectoryFullNew(IDescriptor& inOutDesc) {
	FileCopier<std::string> fc(inOutDesc);
	fc.maincopy();
}

// 1 single file or dir as request (full path)
void deleteFile(IDescriptor& inOutDesc) {
  std::string filepath = readStringWithLen(inOutDesc);
  PRINTUNIFIED("received filepath to delete is:\t%s\n", filepath.c_str());
  
  int ret = genericDeleteBasicImpl(filepath);
  sendBaseResponse(ret, inOutDesc);
}

HRESULT common_compress_logic(Func_CreateObject& createObjectFunc,
                              IDescriptor& inOutDesc,
                              std::string& destArchive,
                              std::string& password,
                              FString& archiveName,
                              compress_rq_options_t& compress_options,
                              CObjectVector<CDirItem>& dirItems,
                              CArchiveUpdateCallback* updateCallbackSpec,
                              size_t filesNum) {
    // check existence of destination archive's parent directory
    // if not existing, create it, if creation fails send error message
    std::string destParentDir = getParentDir(destArchive);
    if (mkpathCopyPermissionsFromNearestAncestor(destParentDir) != 0) {
        PRINTUNIFIEDERROR("Unable to create destination archive's parent directory\n");
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }

    auto outFileStreamSpec = new COutFileStream;
    CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
    if (!outFileStreamSpec->Create(archiveName, false))
    {
        PrintError("can't create archive file");
        // errno = EACCES; // or EEXIST // errno should already be set
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }

    CMyComPtr<IOutArchive> outArchive;
    // get extension of dest file in order to choose archive encoder, return error on not supported extension
    // only 7Z,ZIP and TAR for now
    std::string outExt;
    ArchiveType archiveType = UNKNOWN;
    int extRet = getFileExtension(destArchive,outExt);
    if (extRet < 0) goto unknownOutType;

    archiveType = archiveTypeFromExtension(outExt);
    switch(archiveType) {
        // allowed output types
        case GZ:
        case BZ2:
        case XZ:
            if(filesNum != 1) { // won't filter the case of one single directory, which will gice update error later
                PrintError("Only one input file allowed for creating stream archives");
                errno = 23457;
                sendErrorResponse(inOutDesc);
                return E_ABORT;
            }
        case _7Z:
        case ZIP:
        case TAR:
            goto allowedOutType;
        default:
            goto unknownOutType;
    }

    unknownOutType:
    if (archiveType == UNKNOWN) {
        PrintError("Unable to find any codec associated to the output archive extension for the pathname ", archiveName);
        errno = 23460;
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }

    allowedOutType:
    if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IOutArchive, (void **)&outArchive) != S_OK)
    {
        PrintError("Can not get class object");
        errno = 12344;
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }

    CMyComPtr<IArchiveUpdateCallback2> updateCallback(updateCallbackSpec);

    // TAR does not support password-protected archives, however setting password
    // doesn't result in error (archive is simply not encrypted anyway), so no need
    // to add explicit input parameter check
    updateCallbackSpec->PasswordIsDefined = (!password.empty());
    auto&& password_ = UTF8_to_wchar(password);
    updateCallbackSpec->Password = FString(password_.c_str());

    updateCallbackSpec->Init(&dirItems);

    // ARCHIVE OPTIONS
    // independently from what options are passed to roothelper, the ones
    // which are not-compatible with the target archive format are ignored

    // names
    const wchar_t *names_7z[]
            {
                    L"x", // compression level
                    L"s", // solid mode
                    L"he" // encrypt filenames
            };

    const wchar_t *names_other[]
            {
                    L"x", // compression level
            };

    // values
    NWindows::NCOM::CPropVariant values_7z[3]
            {
                    (UInt32)(compress_options.compressionLevel),	// compression level
                    (compress_options.solid > 0),					// solid mode
                    (compress_options.encryptHeader > 0)			// encrypt filenames
            };

    NWindows::NCOM::CPropVariant values_other[1]
            {
                    (UInt32)(compress_options.compressionLevel)	// compression level
            };

    CMyComPtr<ISetProperties> setProperties;
    outArchive->QueryInterface(IID_ISetProperties, (void **)&setProperties);
    if (!setProperties)
    {
        PrintError("ISetProperties unsupported");
        errno = 12377;
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }
    int setPropertiesResult = 0;
    switch (archiveType) {
        case _7Z:
            setPropertiesResult = setProperties->SetProperties(names_7z, values_7z, 3); // last param: kNumProps
            break;
        case ZIP:
        case GZ:
        case BZ2:
        case XZ:
            setPropertiesResult = setProperties->SetProperties(names_other, values_other, 1);
            break;
        default:
            break; // do not set any option for TAR format
    }

    if(setPropertiesResult) {
        PrintError("Unable to setProperties for archive");
        errno = 12378;
        sendErrorResponse(inOutDesc);
        return E_ABORT;
    }
    // END ARCHIVE OPTIONS

    sendOkResponse(inOutDesc); // means: archive init OK, from now on start compressing and publishing progress

    HRESULT result = outArchive->UpdateItems(outFileStream, filesNum, updateCallback);
    updateCallbackSpec->Finilize();

    return result; // S_FALSE or S_OK
}


void compressToArchiveFromFds(IDescriptor& inOutDesc) {
    auto createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
    if (!createObjectFunc) {
        PrintError("Can not get CreateObject");
        exit(-1);
    }

    auto updateCallbackSpec = new ArchiveUpdateCallbackFromFd(&inOutDesc); // inOutDesc for publishing progress and for receiving fds and stats

    CObjectVector<CDirItem> dirItems; // unused, just for refactoring

    // BEGIN RECEIVE DATA: list of filenames and struct stats from content provider (via JNI)
    PRINTUNIFIED("[fds]receiving stats...");
    updateCallbackSpec->receiveStats();
    PRINTUNIFIED("[fds]receiving stats completed");

    // ...then destArchive
    std::string destArchive = readStringWithLen(inOutDesc);
    PRINTUNIFIED("[fds]received destination archive path is:\t%s\n", destArchive.c_str());

    auto&& destArchive_ = UTF8_to_wchar(destArchive);
    FString archiveName(destArchive_.c_str());

    // receive compress options
    compress_rq_options_t compress_options{};
    readcompress_rq_options(inOutDesc,compress_options);
    PRINTUNIFIED("received compress options:\tlevel %u, encryptHeader %s, solid %s\n",
                 compress_options.compressionLevel,
                 compress_options.encryptHeader?"true":"false",
                 compress_options.solid?"true":"false");

    // read password, if provided
    std::string password = readStringWithByteLen(inOutDesc);
    if (password.empty()) PRINTUNIFIED("No password provided, archive will not be encrypted\n");

    PRINTUNIFIED("Number of items to compress is %zu\n",updateCallbackSpec->fstatsList.size());

    // END RECEIVE DATA (except fds, which are received one at a time when needed during compression)

    // create archive command
    HRESULT result = common_compress_logic(createObjectFunc,
                                           inOutDesc,
                                           destArchive,
                                           password,
                                           archiveName,
                                           compress_options,
                                           dirItems,
                                           updateCallbackSpec,
                                           updateCallbackSpec->fstatsList.size());
    inOutDesc.writeAllOrExit(&maxuint_2,sizeof(uint64_t));

    if (result == E_ABORT) return;
    if (result != S_OK)
    {
        PrintError("Update Error");
        if (errno == 0) errno = 12345;
        sendErrorResponse(inOutDesc);
        return;
    }

    // unreachable in case or error
    FOR_VECTOR(i, updateCallbackSpec->FailedFiles)
    {
        PrintNewLine();
        PrintError("Error for file", updateCallbackSpec->FailedFiles[i]);
    }

    // if (updateCallbackSpec->FailedFiles.Size() != 0) exit(-1);

    sendOkResponse(inOutDesc);

    PRINTUNIFIED("compress completed\n");
    // delete updateCallbackSpec; // commented, causes segfault
}

// segfaults on the third invocation
// not really a problem, since compress task is forked into a new process
// compress or update (TODO) if the destination archive already exists
void compressToArchive(IDescriptor& inOutDesc, uint8_t flags) {
    if(!lib7zLoaded) {
        errno = 771;
        sendErrorResponse(inOutDesc);
        return;
    }
    if(flags) {
        compressToArchiveFromFds(inOutDesc);
        return;
    }
    auto createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
    if (!createObjectFunc) {
        PrintError("Can not get CreateObject");
        exit(-1);
    }

    auto updateCallbackSpec = new CArchiveUpdateCallback;
    updateCallbackSpec->setDesc(&inOutDesc); // for publishing progress

    // BEGIN RECEIVE DATA: srcFolder, destArchive, filenames in srcFolder to compress

    std::vector<std::string> srcDestPaths = readPairOfStringsWithPairOfLens(inOutDesc);
    std::string srcFolder = srcDestPaths[0];
    std::string destArchive = srcDestPaths[1];
    PRINTUNIFIED("received source folder path is:\t%s\n", srcFolder.c_str());
    PRINTUNIFIED("received destination archive path is:\t%s\n", destArchive.c_str());

    auto&& destArchive_ = UTF8_to_wchar(destArchive);
    FString archiveName(destArchive_.c_str());

    // receive compress options
    compress_rq_options_t compress_options{};
    readcompress_rq_options(inOutDesc,compress_options);
    PRINTUNIFIED("received compress options:\tlevel %u, encryptHeader %s, solid %s\n",
                 compress_options.compressionLevel,
                 compress_options.encryptHeader?"true":"false",
                 compress_options.solid?"true":"false");

    // read password, if provided
    std::string password = readStringWithByteLen(inOutDesc);
    if (password.empty()) PRINTUNIFIED("No password provided, archive will not be encrypted\n");

    // receive number of filenames to be received, 0 means compress entire folder content
    uint32_t nOfItemsToCompress;
    inOutDesc.readAllOrExit( &(nOfItemsToCompress), sizeof(uint32_t));

    std::vector<std::string> currentEntries; // actually, this will contain only filenames, not full paths

    PRINTUNIFIED("Number of items to compress is %" PRIu32 "\n",nOfItemsToCompress);

    if (nOfItemsToCompress != 0) {
        currentEntries.reserve(nOfItemsToCompress);
        for (uint32_t i = 0; i < nOfItemsToCompress; i++)
            currentEntries.push_back(readStringWithLen(inOutDesc));
    }
    // END RECEIVE DATA

    // change working directory to srcFolder
    char currentDirectory[1024]{};
    getcwd(currentDirectory, 1024);

    int ret = chdir(srcFolder.c_str()); // errno already set if chdir fails

    if (ret != 0)
    {
        PRINTUNIFIEDERROR("Unable to chdir in compressToArchive, error is %d\n",errno);
        errno = 12340;
        sendErrorResponse(inOutDesc);
        return;
    }

    // create archive command
    ///////////////////////////////////////////////////////////////////////////
    // for each item in currentEntries, if the item is a file, simply add it to dirItems,
    // else create a stdfsIterator and add all content to dirItems

    CObjectVector<CDirItem> dirItems;
    {
        if (nOfItemsToCompress == 0) {
            auto&& dirIt = itf.createIterator(srcFolder, RELATIVE_WITHOUT_BASE, false);
            while (dirIt.next()) {
                std::string curEntString = dirIt.getCurrent();

                CDirItem di;
                FString name = CmdStringToFString(curEntString.c_str());
                auto&& uws = UTF8_to_wchar(curEntString);
                UString u(uws.c_str());

                NFind::CFileInfo fi;
                if (!fi.Find(name)) {
                    PrintError("Can't find file", name);
                    errno = 12341;
                    sendErrorResponse(inOutDesc);
                    return;
                }

                di.Attrib = fi.Attrib;
                di.Size = fi.Size;
                di.CTime = fi.CTime;
                di.ATime = fi.ATime;
                di.MTime = fi.MTime;
                //~ di.Name = fs2us(name);
                di.Name = u;
                di.FullPath = u;
                //~ di.FullPath = name;
                dirItems.Add(di);
            }
        }
        else {
            for (int i = 0; i < nOfItemsToCompress; i++) {
                // stat current item, if it's a directory, open a stdfsIterator over it with parent prepend mode
                struct stat st{};
                stat(currentEntries[i].c_str(), &st); // filepath is relative path (filename only)
                if (S_ISDIR(st.st_mode)) {
                    // STDFSITERATOR ONLY ALLOWS ABSOLUTE PATHS
                    std::stringstream ss;
                    ss<<srcFolder<<"/"<<currentEntries[i];
                    auto&& dirIt = itf.createIterator(ss.str(), RELATIVE_INCL_BASE, false);
                    while (dirIt.next()) {
                        std::string curEntString = dirIt.getCurrent();
                        PRINTUNIFIED("Current item: %s\n",curEntString.c_str());

                        CDirItem di;

                        FString name = CmdStringToFString(curEntString.c_str());
                        auto&& uws = UTF8_to_wchar(curEntString);
                        UString u(uws.c_str());

                        NFind::CFileInfo fi;
                        if (!fi.Find(name)) // !fi.Find(name)
                        {
                            PrintError("Can't find file", name);
                            errno = 12342;
                            sendErrorResponse(inOutDesc);
                            return;
                        }

                        di.Attrib = fi.Attrib;
                        di.Size = fi.Size;
                        di.CTime = fi.CTime;
                        di.ATime = fi.ATime;
                        di.MTime = fi.MTime;
                        //~ di.Name = fs2us(name);
                        di.Name = u;
                        di.FullPath = u;
                        //~ di.FullPath = name;
                        dirItems.Add(di);
                    }
                }
                else { // simply add to dirItems
                    CDirItem di;

                    FString name = CmdStringToFString(currentEntries[i].c_str());
                    auto&& uws = UTF8_to_wchar(currentEntries[i]);
                    UString u(uws.c_str());

                    NFind::CFileInfo fi;
                    if (!fi.Find(name)) {
                        PrintError("Can't find file", name);
                        errno = 12343;
                        sendErrorResponse(inOutDesc);
                        return;
                    }

                    di.Attrib = fi.Attrib;
                    di.Size = fi.Size;
                    di.CTime = fi.CTime;
                    di.ATime = fi.ATime;
                    di.MTime = fi.MTime;
                    //~ di.Name = fs2us(name);
                    di.Name = u;
                    di.FullPath = u;
                    //~ di.FullPath = name;
                    dirItems.Add(di);
                }
            }
        }
    }

    HRESULT result = common_compress_logic(createObjectFunc,
                                           inOutDesc,
                                           destArchive,
                                           password,
                                           archiveName,
                                           compress_options,
                                           dirItems,
                                           updateCallbackSpec,
                                           dirItems.Size());
    if (result == E_ABORT) return;
    if (result != S_OK) {
        PrintError("Update Error");
        if (errno == 0) errno = 12345;
        sendEndProgressAndErrorResponse(inOutDesc);
        return;
    }

    // unreachable in case or error
    FOR_VECTOR(j, updateCallbackSpec->FailedFiles) {
        PrintNewLine();
        PrintError("Error for file", updateCallbackSpec->FailedFiles[j]);
    }

    // if (updateCallbackSpec->FailedFiles.Size() != 0) exit(-1);

    sendEndProgressAndOkResponse(inOutDesc);

    // restore old working directory
    ret = chdir(currentDirectory);
    PRINTUNIFIED("compress completed\n");
    // delete updateCallbackSpec; // commented, causes segfault
}

inline void toUppercaseString(std::string& strToConvert) {
	std::transform(strToConvert.begin(), strToConvert.end(), strToConvert.begin(), ::toupper);
}

std::string toUppercaseStringCopy(const std::string& strToConvert) {
	std::string ret = strToConvert;
	toUppercaseString(ret);
	return ret;
}

std::string getVirtualInnerNameForStreamArchive(const std::string& archiveName, ArchiveType archiveType) {
	std::string uppercaseString = toUppercaseStringCopy(archiveName);
	std::string ext;
	switch (archiveType) {
		case XZ:
			ext = ".XZ";
			break;
		case GZ:
			ext = ".GZ";
			break;
		case BZ2:
			ext = ".BZ2";
		default:
			break;
	}
	
	return has_suffix(uppercaseString,ext)?
									archiveName.substr(0,archiveName.length()-ext.length()):
									archiveName;
}

/*
 - on extract, p7zip takes as input a list of indexes to be extracted (they correspond to nodes without children,
that is, files or folders themselves without their sub-tree nodes)
- XFiles UI allows to choose some items at a subdirectory level in an archive, but if an item is a directory, XFiles sends
 all the entries of its subtree (the entire archive content tree has already been stored in a VMap on archive open for listing)
- if items are not top-level in the archive, the path truncation offset is needed in order to perform natural relative extraction
 */
void extractFromArchive(IDescriptor& inOutDesc)
{
    if(!lib7zLoaded) {
        errno = 771;
        sendErrorResponse(inOutDesc);
        return;
    }

	auto createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
	if (!createObjectFunc)
	{
		PrintError("Can not get CreateObject");
		errno = 12346;
		sendErrorResponse(inOutDesc);
		return;
	}
	// recognizes archive type and tries to extract selected or all files into the destination folder
	
	// read source archive and destination folder
	std::vector<std::string> f = readPairOfStringsWithPairOfLens(inOutDesc);
	std::string srcArchive = f[0];
	std::string destFolder = f[1];
	PRINTUNIFIED("received source archive path is:\t%s\n", srcArchive.c_str());
	PRINTUNIFIED("received destination folder path is:\t%s\n", destFolder.c_str());
	
	// read password, if provided	
	std::string password = readStringWithByteLen(inOutDesc);

	// read number of entries to be received; 0 means "extract all content from archive"
	uint32_t nOfEntriesToExtract, i;
	inOutDesc.readAllOrExit( &(nOfEntriesToExtract), sizeof(uint32_t));
  
	std::vector<uint32_t> currentEntries;
	
	PRINTUNIFIED("nOfEntriesToExtract: %d\n", nOfEntriesToExtract);
	uint32_t subDirLengthForPathTruncateInWideChars = 0;
	if (nOfEntriesToExtract != 0)
	{
		// DO NOT RECEIVE FILENAME ENTRIES, RECEIVE INDICES INSTEAD - TODO check if works with all archive types
		currentEntries.reserve(nOfEntriesToExtract);
		for (i = 0; i < nOfEntriesToExtract; i++) {
			uint32_t current;
			inOutDesc.readAllOrExit( &current, sizeof(uint32_t));
			currentEntries.push_back(current);
		}

        // RECEIVE THE PATH LENGTH TO TRUNCATE FOR RELATIVE SUBDIR EXTRACTION
        inOutDesc.readAllOrExit(&subDirLengthForPathTruncateInWideChars,sizeof(uint32_t));
	}

	auto&& srcArchive_ = UTF8_to_wchar(srcArchive);
	FString archiveName(srcArchive_.c_str());
	
	CMyComPtr<IInArchive> archive;
	
	ArchiveType archiveType = detectArchiveType(srcArchive);
	
	// PRINTUNIFIED("Archive type is: %u\n",archiveType);
	if (archiveType == UNKNOWN) {
		PrintError("Unable to open archive with any associated codec", archiveName);
		errno = 23458;
		sendErrorResponse(inOutDesc);
		return;
	}
  
	if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IInArchive, (void **)&archive) != S_OK) {
		PrintError("Can not get class object");
		errno = 23459;
		sendErrorResponse(inOutDesc);
		return;
	}
  

	auto fileSpec = new CInFileStream;
	CMyComPtr<IInStream> file = fileSpec;

	if (!fileSpec->Open(archiveName)) {
		PrintError("Can not open archive file", archiveName);
		errno = EACCES; // simulate access error to file in errno
		sendErrorResponse(inOutDesc);
		return;
	}

	{
    auto openCallbackSpec = new CArchiveOpenCallback;
    CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
    openCallbackSpec->PasswordIsDefined = (!password.empty());
    auto&& password_ = UTF8_to_wchar(password);
    openCallbackSpec->Password = FString(password_.c_str());

    constexpr UInt64 scanSize = 1 << 23;
    if (archive->Open(file, &scanSize, openCallback) != S_OK)
    {
      PrintError("Can not open file as archive - listing error (password needed or wrong password provided?)", archiveName);
      errno = NULL_OR_WRONG_PASSWORD;
      sendErrorResponse(inOutDesc);
      return;
    }
	}
  
  // create destination folder if it does not exist
  int ret = mkpathCopyPermissionsFromNearestAncestor(destFolder);
  if (ret < 0) {
	  errno = EACCES;
	  PRINTUNIFIEDERROR("Can not create destination folder %s\n",destFolder.c_str());
	  sendErrorResponse(inOutDesc);
	  return;
  }

  // Extract command
  auto extractCallbackSpec = new CArchiveExtractCallback;
  // generate virtual single-child name by archive name in case of stream archive types
  if (archiveType == XZ || archiveType == GZ || archiveType == BZ2)
      extractCallbackSpec->streamArchiveOnlyChildFilenameOnly = UTF8_to_wchar(
              getVirtualInnerNameForStreamArchive(getFilenameFromFullPath(srcArchive),archiveType));
  
  extractCallbackSpec->setDesc(&inOutDesc); // for publishing progress
  extractCallbackSpec->setSubDirLengthForPathTruncate(subDirLengthForPathTruncateInWideChars);
  
  CMyComPtr<IArchiveExtractCallback> extractCallback(extractCallbackSpec);
  
  sendOkResponse(inOutDesc); // means: archive init OK, from now on start extracting and publishing progress

  auto&& destFolder_ = UTF8_to_wchar(destFolder);
  auto&& password_ = UTF8_to_wchar(password);
  extractCallbackSpec->Init(archive, FString(destFolder_.c_str()));
  extractCallbackSpec->PasswordIsDefined = (!password.empty());
  extractCallbackSpec->Password = FString(password_.c_str());

  HRESULT result;

  if (currentEntries.empty())
  { // extract all
    result = archive->Extract(nullptr, (UInt32)(Int32)(-1), false, extractCallback);
  }
  else
  { // extract only the received entries
    result = archive->Extract(&currentEntries[0], nOfEntriesToExtract, false, extractCallback);
  }
  
  if (latestExtractResult != NArchive::NExtract::NOperationResult::kOK) {
	  PRINTUNIFIEDERROR("Extraction error (wrong password or something else), code: %d\n",latestExtractResult);
	  errno = latestExtractResult;
	  latestExtractResult = 0;
	  
	  sendEndProgressAndErrorResponse(inOutDesc);
      return;
  }

  if (result == S_OK)
    result = extractCallbackSpec->SetFinalAttribs();
    
  if (result != S_OK) {
	  PrintError("Extraction error (password needed or wrong password provided?)", archiveName);
      errno = NULL_OR_WRONG_PASSWORD;
      sendEndProgressAndErrorResponse(inOutDesc);
      return;
  }
  sendEndProgressAndOkResponse(inOutDesc);
  // delete extractCallbackSpec; // commented, causes segfault
}

// for GZ,BZ2,XZ archives (that contains only one file stream with no attributes)
// do not even check header, will be checked on extract, just return name without extension
void listStreamArchive(IDescriptor& inOutDesc, const std::string& archiveName, ArchiveType archiveType) {
	std::string virtualInnerName = getVirtualInnerNameForStreamArchive(archiveName, archiveType);
									
	inOutDesc.writeAllOrExit( &RESPONSE_OK, sizeof(uint8_t));
	
	ls_resp_t responseEntry{};
	responseEntry.filename = virtualInnerName;
	memset(responseEntry.permissions, '-', 10);
	
	writeLsRespOrExit(inOutDesc,responseEntry);
	
	uint16_t terminationLen = 0;
	inOutDesc.writeAllOrExit( &terminationLen, sizeof(uint16_t));
}

// list compressed archive returning all entries (at every archive directory tree level) as relative-to-archive paths
// error code is not indicative in errors generated in this function, errno is set manually before calling sendErrorResponse
// only UTF-8 codepoint 0 contains the \0 byte (unlike UTF-16)
// web source: http://stackoverflow.com/questions/6907297/can-utf-8-contain-zero-byte
void listArchive(IDescriptor& inOutDesc) {
    if(!lib7zLoaded) {
        errno = 771;
        sendErrorResponse(inOutDesc);
        return;
    }
	auto createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
	if (!createObjectFunc) {
		PrintError("Can not get CreateObject");
		exit(-1);
	}
	
	// read input from socket (except first byte - action code - which has already been read by caller, in order to perform action dispatching)
	std::string archivepath = readStringWithLen(inOutDesc);
	
	// read password, if provided
	std::string password = readStringWithByteLen(inOutDesc);
	
	// perform list archive
	auto&& archivepath_ = UTF8_to_wchar(archivepath);
	FString archiveName(archivepath_.c_str());
	
	CMyComPtr<IInArchive> archive;
	
	ArchiveType archiveType = detectArchiveType(archivepath);
	// PRINTUNIFIED("Archive type is: %u\n",archiveType);
	if (archiveType == UNKNOWN) {
		PrintError("Unable to open archive with any associated codec", archiveName);
		errno = 23458;
		sendErrorResponse(inOutDesc);
		return;
	}
	
	// for stream archives, just send a virtual inner name without actually opening the archive
	switch (archiveType) {
		case XZ:
		case GZ:
		case BZ2:
			listStreamArchive(inOutDesc,getFilenameFromFullPath(archivepath),archiveType);
			return;
		default:
			break;
	}
	
	if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IInArchive, (void **)&archive) != S_OK) {
	PrintError("Can not get class object");
	errno = 23459;
	sendErrorResponse(inOutDesc);
	return;
	}

	//////////////////////////////////////
	
	auto fileSpec = new CInFileStream;
	CMyComPtr<IInStream> file = fileSpec;
	
	if (!fileSpec->Open(archiveName)) {
		PrintError("Can not open archive file", archiveName);
		errno = EACCES; // simulate access error to file in errno
		sendErrorResponse(inOutDesc);
		return;
	}
	
	{
		auto openCallbackSpec = new CArchiveOpenCallback;
		CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
		
		openCallbackSpec->PasswordIsDefined = (!password.empty());
		auto&& password_ = UTF8_to_wchar(password);
		openCallbackSpec->Password = FString(password_.c_str());

		constexpr UInt64 scanSize = 1 << 23;
		if (archive->Open(file, &scanSize, openCallback) != S_OK) {
			PrintError("Can not open file as archive - listing error (password needed or wrong password provided?)", archiveName);
			errno = NULL_OR_WRONG_PASSWORD;
			sendErrorResponse(inOutDesc);
			return;
		}
	}
      
	////////////////////////////////////////////

	// Enumerate and send archive entries
	UInt32 numItems = 0;
	archive->GetNumberOfItems(&numItems);
	
	PRINTUNIFIED("Entering entries enumeration block, size is %u\n", numItems); // DEBUG
	
	inOutDesc.writeAllOrExit( &RESPONSE_OK, sizeof(uint8_t));
	
	for (UInt32 i = 0; i < numItems; i++) {
		// assemble and send response
		ls_resp_t responseEntry{};
		{
			// Get uncompressed size of file
			NCOM::CPropVariant prop;
			archive->GetProperty(i, kpidSize, &prop);
			char s[32]{};
			ConvertPropVariantToShortString(prop, s);
			responseEntry.size = atol(s);
		}
    //~ {
		//~ // Get compressed size of file (TO BE TESTED)
		//~ NCOM::CPropVariant prop;
		//~ archive->GetProperty(i, kpidPackSize, &prop);
		//~ char s[32]{};
		//~ ConvertPropVariantToShortString(prop, s);
		//~ // responseEntry.packedSize = atol(s); // TODO modify responseEntry struct before
	//~ }
		{
			// Get name of file
			NCOM::CPropVariant prop;
			archive->GetProperty(i, kpidPath, &prop);
			if (prop.vt == VT_BSTR) {
                UString u;
				u.SetFromBstr(prop.bstrVal);
				const wchar_t* uuu = u.Ptr(); // wchar on Linux is 4 bytes long
				responseEntry.filename = wchar_to_UTF8(uuu);
				PRINTUNIFIED("entry name: %s\n", responseEntry.filename.c_str()); // DEBUG
			}
			else if (prop.vt != VT_EMPTY) {
				responseEntry.filename = "ERROR";
			}
		}
		{
			// Get modified timestamp
			NCOM::CPropVariant prop;
			archive->GetProperty(i, kpidMTime, &prop);
			if (prop.vt == VT_FILETIME) {
				int64_t tmpTimeBuffer;
				memcpy(&tmpTimeBuffer,&(prop.filetime),sizeof(int64_t));
				responseEntry.date = convertWindowsTimeToUnixTime(tmpTimeBuffer);
			}
		}
	//~ {
		//~ // Get permissions NOT WORKING
		//~ NCOM::CPropVariant prop;
		//~ archive->GetProperty(i, kpidAttrib, &prop);
		//~ if (prop.vt == VT_UI4) {
			//~ // PRINTUNIFIED("ulval is %u\n",prop.ulVal);
			//~ getPermissions(responseEntry.permissions,prop.ulVal);
		//~ }
		//~ else {
		//~ // else treat as regular file even if property is missing TODO even better skip current entry
		//~ memset(responseEntry.permissions, '-', 10);
		//~ }
	//~ }
		{
			// Get only dir attribute
			NCOM::CPropVariant prop;
			archive->GetProperty(i, kpidIsDir, &prop);
			if (prop.vt == VT_BOOL) {
				if (prop.boolVal != VARIANT_FALSE) memcpy(responseEntry.permissions,"d---------",10);
				else memset(responseEntry.permissions, '-', 10);
			}
			else {
				// else treat as regular file even if property is missing TODO even better skip current entry
				memset(responseEntry.permissions, '-', 10);
			}
		}
		
		writeLsRespOrExit(inOutDesc,responseEntry);
	}
	
	// list termination indicator
	uint16_t terminationLen = 0;
	inOutDesc.writeAllOrExit( &terminationLen, sizeof(uint16_t));
}

// switch using flags
void listDirOrArchive(IDescriptor& inOutDesc, uint8_t flags) {
	switch(flags) {
		case 0: // 000
			listDir(inOutDesc);
			break;
		case 2: // 010
			retrieveHomePath(inOutDesc);
			break;
		case 7: // 111
			{
				auto& pd = static_cast<PosixDescriptor&>(inOutDesc);
				listArchive(pd);
				break;
			}
		default:
			PRINTUNIFIEDERROR("Unexpected flags in listdir");
			threadExit();
	}
}

const char* find_legend = R"FIND1(
/*
 * FIND request:
 *  - Input: source path (dir or archive), filename pattern (can be empty), content pattern (can be empty)
 *  - Flag bits:
 * 				b0 - (Overrides other bits) Cancel current search, if any
 * 				b1 - Search in subfolders only (if false, search recursively)
 * 				b2 - Search in archives (search within archives if true) - CURRENTLY IGNORED
 *  - Two additional bytes for pattern options:
 * 				- For filename pattern:
 * 				b0 - Use regex (includes escaped characters, case-sensitivity options and whole word options) - CURRENTLY IGNORED
 * 				b1 - Use escaped characters (if b0 true, b1 is ignored) - CURRENTLY IGNORED
 * 				b2 - Case-insensitive search if true (if b0 true, b2 is ignored)
 * 				b3 - Whole-word search (if b0 true, b3 is ignored) - CURRENTLY IGNORED
 * 				- For content pattern:
 * 				b4,b5,b6,b7 same as b0,b1,b2,b3
 * 				b8 (b0 of second byte): find all occurrences in content
 */
)FIND1";

std::mutex currentSearchInOutDesc_mx;
IDescriptor* currentSearchInOutDesc = nullptr; // at most one search thread per roothelper instance

void find_thread_exit_function_unsafe() {
	if(currentSearchInOutDesc) {
		currentSearchInOutDesc->close(); // will cause search thread, if any, to exit on socket write error, in so freeing the allocated global IDescriptor
		delete currentSearchInOutDesc;
		currentSearchInOutDesc = nullptr;
	}
}

void on_find_thread_exit_function(bool resourceAlreadyLocked = false) {
	PRINTUNIFIED("Resetting find thread global descriptor\n");
	if(resourceAlreadyLocked)
		find_thread_exit_function_unsafe();
	else {
		std::unique_lock<std::mutex> lock(currentSearchInOutDesc_mx);
		find_thread_exit_function_unsafe();
	}
}

void findNamesAndContent(IDescriptor& inOutDesc, uint8_t flags) {
	// BEGIN DEBUG
	// print rq flags
	PRINTUNIFIED("Find rq flags:\n");
	PRINTUNIFIED("b0 %d\n",b0(flags));
	PRINTUNIFIED("b1 %d\n",b1(flags));
	PRINTUNIFIED("b2 %d\n",b2(flags));
	// END DEBUG
	
	std::unique_lock<std::mutex> lock(currentSearchInOutDesc_mx);
	if (b0(flags)) { // cancel current search, if any
		on_find_thread_exit_function(true); // force close of currently open search descriptor
		sendOkResponse(inOutDesc);
		return;
	}
	
	if (currentSearchInOutDesc) { // another search task already active, and we are trying to start a new one
		errno = EAGAIN; // try again later
		lock.unlock();
		sendErrorResponse(inOutDesc);
		return;
	}
	
	// read additional bytes with search options
	uint16_t searchFlags;
	inOutDesc.readAllOrExit(&searchFlags,sizeof(uint16_t));
	
	// BEGIN DEBUG
	// print search flags
	PRINTUNIFIED("Find option flags:\n");
	for (uint8_t i=0;i<9;i++)
		PRINTUNIFIED("%d: %d\n",i,BIT(searchFlags,i));
	// END DEBUG
	
	// read common request
	std::string basepath = readStringWithLen(inOutDesc);
	PRINTUNIFIED("received base path to look up in is:\t%s\n", basepath.c_str());
	std::string namepattern = readStringWithLen(inOutDesc);
	if (!namepattern.empty())
		PRINTUNIFIED("received name pattern to search is:\t%s\n", namepattern.c_str());
	std::string contentpattern = readStringWithLen(inOutDesc);
	if (!contentpattern.empty())
		PRINTUNIFIED("received content pattern to search is:\t%s\n", contentpattern.c_str());
	
	
	// perform search
	// check on every listdir command the value of currentSearchInOutDesc, other than exiting on write error
	// (because otherwise a long-term search that doesn't find anything would continue despite the socket has been closed, since it
	// would send no updates till the end-of-list indication)
	
	auto& fpd = dynamic_cast<PosixDescriptor&>(inOutDesc);

	currentSearchInOutDesc = new PosixDescriptor(fpd.desc);
	lock.unlock();
	
	sendOkResponse(inOutDesc);
	
	// check flags
	if(b1(flags)) { // search in base folder only
		PRINTUNIFIED("Entering plain listing...\n");
		DIR *d;
		struct dirent *dir;
		d = opendir(basepath.c_str());
		if(d) {
			while ((dir = readdir(d)) != nullptr) {
				// exclude current (.) and parent (..) directory
				if (strcmp(dir->d_name, ".") == 0 ||
					strcmp(dir->d_name, "..") == 0) continue;
					
				if (currentSearchInOutDesc == nullptr) {
					PRINTUNIFIEDERROR("Search interrupted,exiting...\n");
					threadExit();
				}
				
				std::string haystack = std::string(dir->d_name);
				std::string needle = namepattern;
				if (b2(searchFlags)) {
					toUppercaseString(haystack);
					toUppercaseString(needle);
				}
				if (haystack.find(needle) != std::string::npos) {
					find_resp_t findEntry{};
					
					std::string filepathname = pathConcat(basepath,dir->d_name);
					if (assemble_ls_resp_from_filepath(filepathname,dir->d_name,findEntry.fileItem)!=0) {
						PRINTUNIFIEDERROR("Unable to stat file path: %s\n",filepathname.c_str());
						continue;
					}
					
					// no content around nor content offset
					if (writefind_resp(inOutDesc,findEntry) < 0) {
						threadExit();
					}
				}
			}
			closedir(d);
		}
	}
	else { // search in subfolders
		PRINTUNIFIED("Entering recursive listing...\n");
		auto&& dirIt = itf.createIterator(basepath, FULL, true, SMART_SYMLINK_RESOLUTION);
		while (dirIt.next()) {
			std::string curEntString = dirIt.getCurrent();
			std::string curEntName = dirIt.getCurrentFilename();
			
			if (currentSearchInOutDesc == nullptr) {
				PRINTUNIFIEDERROR("Search interrupted,exiting...\n");
				threadExit();
			}
			
			std::string haystack = curEntName;
			std::string needle = namepattern;
			if (b2(searchFlags)) {
				toUppercaseString(haystack);
				toUppercaseString(needle);
			}
			if (haystack.find(needle) != std::string::npos) {
				find_resp_t findEntry{};
				
				if (assemble_ls_resp_from_filepath(curEntString,curEntString,findEntry.fileItem)!=0) {
					PRINTUNIFIEDERROR("Unable to stat file path: %s\n",curEntString.c_str());
					continue;
				}
				
				// no content around nor content offset
				if (writefind_resp(inOutDesc,findEntry) < 0) {
					threadExit();
				}
			}
		}
	}
	
	// send end of list indication
	constexpr uint16_t eol = 0;
	inOutDesc.writeAllOrExit(&eol,sizeof(uint16_t));
	on_find_thread_exit_function(); // free search descriptor
}

#ifdef __linux__
bool authenticatePeer(int nativeDesc) {
  struct ucred cred{}; // members: pid_t pid, uid_t uid, gid_t gid
  socklen_t len = sizeof(cred);

  if (getsockopt(nativeDesc, SOL_SOCKET, SO_PEERCRED, &cred, &len) < 0) {
	  PRINTUNIFIED("Unable to getsockopt for peer cred\nPeer authentication failed\n");
	  return false;
  }
  if (cred.uid == CALLER_EUID) {
	  PRINTUNIFIED("Peer authentication successful\n");
	  return true;
  }
  else {
	  PRINTUNIFIED("Peer authentication failed\n");
	  return false;
  }
}
#else
bool authenticatePeer(int nativeDesc) {
  uid_t uid;
  gid_t gid;
  if (getpeereid(nativeDesc, &uid, &gid) < 0) {
	  PRINTUNIFIED("Unable to getsockopt for peer cred\nPeer authentication failed\n");
	  return false;
  }
  if (uid == CALLER_EUID) {
	  PRINTUNIFIED("Peer authentication successful\n");
	  return true;
  }
  else {
	  PRINTUNIFIED("Peer authentication failed\n");
	  return false;
  }
}
#endif
////////////////////////////////////

void killProcess(IDescriptor& inOutDesc) {
	// errno = ESRCH means target pid does not exist
	uint32_t pid,sig;
	inOutDesc.readAllOrExit(&pid,sizeof(uint32_t));
	inOutDesc.readAllOrExit(&sig,sizeof(uint32_t));
	PRINTUNIFIED("invoking killProcess on pid %d with signal %d\n",pid,sig);
	int ret = kill(pid,sig);
	sendBaseResponse(ret,inOutDesc);
}

void getThisPid(IDescriptor& inOutDesc) {
	pid_t pid = getpid(); // getpid can never fail, no need to send OK/ERROR byte
	inOutDesc.writeAllOrExit(&pid,sizeof(uint32_t));
}

// currently, only TRUNCATE + UNATTENDED mode (overwrite file, write/read till other endpoint closes connection)
// TODO flags 3 bits: 0: WRITE/READ, 1: TRUNCATE/APPEND, 2: ATTENDED/UNATTENDED
// also one byte in read for known-in-advance-size (return error on streams for which there is no known size) vs stream-mode (no file size info requested)
void readOrWriteFile(IDescriptor& inOutDesc, uint8_t flags) {
	std::string filepath = readStringWithLen(inOutDesc);
	PRINTUNIFIED("received filepath to stream is:\t%s\n", filepath.c_str());
	
	std::vector<uint8_t> iobuffer(COPY_CHUNK_SIZE);
	int ret;
    
	int readBytes,writtenBytes;
	if (flags) { // read from file, send to client
		// UNATTENDED READ: at the end of the file close connection
		PRINTUNIFIED("Read from file, send to client\n");
		struct stat st{};
		ret = getFileType_(filepath.c_str(),&st);
		if (ret < 0) {
			errno = ENOENT;
			sendErrorResponse(inOutDesc);
			return;
		}
		if (ret == DT_DIR) {
			errno = EISDIR;
			sendErrorResponse(inOutDesc);
			return;
		}
		
		auto&& fd = fdfactory.create(filepath,FileOpenMode::READ);
		if (!fd) {
			sendErrorResponse(inOutDesc);
			return;
		}
		
		sendOkResponse(inOutDesc);
		for(;;) {
			readBytes = fd.read(&iobuffer[0],COPY_CHUNK_SIZE);
			if (readBytes <= 0) {
				PRINTUNIFIEDERROR("break, read byte count is %d, errno is %d\n",readBytes,errno);
				break;
			}
			writtenBytes = inOutDesc.writeAll(&iobuffer[0],readBytes);
			if (writtenBytes < readBytes) {
				PRINTUNIFIEDERROR("break, written byte count is %d, errno is %d\n",writtenBytes,errno);
				break;
			}
		}
		
		fd.close();
	}
	else { // receive from client, write to file
		// UNATTENDED READ: on remote connection close, close the file
		PRINTUNIFIED("Receive from client, write to file\n");
		
		auto&& fd = fdfactory.create(filepath,FileOpenMode::WRITE);
		if (!fd) {
			sendErrorResponse(inOutDesc);
			return;
		}
		
		sendOkResponse(inOutDesc);

		// start streaming file from client till EOF or any error
		PRINTUNIFIED("Receiving stream...\n");
		for(;;) {
			readBytes = inOutDesc.read(&iobuffer[0],COPY_CHUNK_SIZE);
			if (readBytes <= 0) {
				PRINTUNIFIEDERROR("break, read byte count is %d\n",readBytes);
				break;
			}
			writtenBytes = fd.writeAll(&iobuffer[0],readBytes);
			if (writtenBytes < readBytes) {
				PRINTUNIFIEDERROR("break, written byte count is %d\n",writtenBytes);
				break;
			}
		}
		
		fd.close();
	}
	inOutDesc.close();
	threadExit(); // FIXME almost certainly not needed
}


/*
 * 2 flag bits, 1 for access date, 1 for modified date (creation date modification not supported)
 * enable receiving of at most 2 timestamps (receive uint32 seconds, assign to time_t, nanosecond to 0 -> into struct timespec)
 */
void setDates(const char* filepath, IDescriptor& inOutDesc,uint8_t flags) {
	PRINTUNIFIED("Setting file dates\n");
	PRINTUNIFIED("Flags: %u\n",flags);
	
	struct timeval times[2]{};
	
	uint32_t x;
	if (b0(flags)) { // access (least significant bit)
		inOutDesc.readAllOrExit(&x,sizeof(uint32_t));
		times[0].tv_sec = x;
	}
	if (b1(flags)) { // modification (second least significant bit)
		inOutDesc.readAllOrExit(&x,sizeof(uint32_t));
		times[1].tv_sec = x;
	}
	
	struct stat st{}; // in case of expected modification of only one timestamp, take the other from here
	int ret = getFileType_(filepath,&st);
	if (ret < 0) {
		sendErrorResponse(inOutDesc);
		return;
	}
	
	// complete time structs if the user has chosen not to set both timestamps
	if (!b0(flags)) times[0].tv_sec = st.st_atime;
	if (!b1(flags)) times[1].tv_sec = st.st_mtime;
	
	ret = utimes(filepath,times);
	sendBaseResponse(ret,inOutDesc);
}

/* two flag bits that enable receiving at most both uid_t and gid_t */
void setOwnership(const char* filepath, IDescriptor& inOutDesc,uint8_t flags) {
	PRINTUNIFIED("Setting file ownership\n");
	PRINTUNIFIED("Flags: %u\n",flags);
	int32_t owner = -1,group = -1;
	if (b0(flags)) inOutDesc.readAllOrExit(&owner,sizeof(int32_t));
	if (b1(flags)) inOutDesc.readAllOrExit(&group,sizeof(int32_t));
	
	struct stat st{}; // in case of expected modification of only one between owner and group, take the other from here
	int ret = getFileType_(filepath,&st);
	if (ret < 0) {
		sendErrorResponse(inOutDesc);
		return;
	}
	
	if (!b0(flags)) owner = st.st_uid;
	if (!b1(flags)) group = st.st_gid;
	
	ret = chown(filepath,owner,group);
	sendBaseResponse(ret,inOutDesc);
}

/* no flag bits used, only param to receive: mode_t with permissions */
void setPermissions(const char* filepath, IDescriptor& inOutDesc) {
	PRINTUNIFIED("Setting file permissions\n");
	int32_t perms;
	inOutDesc.readAllOrExit(&perms,sizeof(int32_t)); // mode_t is not always 4 bytes (see comment in createFileOrDirectory function)
	
	int ret = chmod(filepath, perms);
	sendBaseResponse(ret,inOutDesc);
}

/*
 * called upon ACTION_SETATTRIBUTES action byte
 * ignore flags, receive one additional byte, switch on first 2 bits (MSB):
 * 	0 -> setFileDates
 *  1 -> setFileOwnership
 *  2 -> setFilePermissions
 * rebuild flags to be passed from this byte, taken from last 2 bits LSB
 */
void setAttributes(IDescriptor& inOutDesc) {
	uint8_t b;
	inOutDesc.readAllOrExit(&b, sizeof(uint8_t));
	uint8_t b1 = (b & (3 << 6)) >> 6; // MSB 2 bits
	uint8_t flags = b & 3; // LSB 2 bits
	
	std::string filepath = readStringWithLen(inOutDesc);
	PRINTUNIFIED("received filepath to change attributes is:\t%s\n", filepath.c_str());
	
	switch(b1) {
		case 0:
			setDates(filepath.c_str(),inOutDesc,flags);
			break;
		case 1:
			setOwnership(filepath.c_str(),inOutDesc,flags);
			break;
		case 2:
			setPermissions(filepath.c_str(),inOutDesc);
			break;
		default:
			PRINTUNIFIEDERROR("Unexpected sub-action byte");
			threadExit();
	}
}

////////////////////////////////////////////////////////////////////////
// Remote protocol base functions (op code byte already received in caller)
////////////////////////////////////////////////////////////////////////

void client_createFileOrDirectory(IDescriptor& cl, IDescriptor& rcl, request_type rqByteWithFlags) {
	uint32_t mode;
	uint8_t creationStrategy;
	uint64_t filesize;
	std::string dirpath = readStringWithLen(cl);
	cl.readAllOrExit(&mode,4);

    BufferedWriteDescriptor brcl(rcl);
	brcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	writeStringWithLen(brcl,dirpath);
	brcl.writeAllOrExit(&mode,4);
	
	// switch additional content to propagate depending on flags
	if(b0(rqByteWithFlags.flags)) {
		if(b1(rqByteWithFlags.flags)) {
			cl.readAllOrExit(&creationStrategy,sizeof(uint8_t));
			cl.readAllOrExit(&filesize,sizeof(uint64_t));
			brcl.writeAllOrExit(&creationStrategy,sizeof(uint8_t));
			brcl.writeAllOrExit(&filesize,sizeof(uint64_t));
		}
		else {
			// nothing to propagate here
		}
	}
	else {
		// nothing to propagate here
	}
    brcl.flush();

	// read and pass-through response (OK or error)
	uint8_t resp;
	int receivedErrno;
	rcl.readAllOrExit(&resp,sizeof(uint8_t));
	if (resp != 0) { // create error
		rcl.readAllOrExit(&receivedErrno,sizeof(int));
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
		cl.writeAllOrExit(&receivedErrno,sizeof(int));
	}
	else { // OK
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
	}
}

void client_createHardOrSoftLink(IDescriptor& cl, IDescriptor& rcl, request_type rqByteWithFlags) {
	std::vector<std::string> srcDestPaths = readPairOfStringsWithPairOfLens(cl);
	BufferedWriteDescriptor brcl(rcl);
    brcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	writePairOfStringsWithPairOfLens(brcl,srcDestPaths);
    brcl.flush();
	
	// read and pass-through response (OK or error)
	uint8_t resp;
	int receivedErrno;
	rcl.readAllOrExit(&resp,sizeof(uint8_t));
	if (resp != 0) { // create error
		rcl.readAllOrExit(&receivedErrno,sizeof(int));
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
		cl.writeAllOrExit(&receivedErrno,sizeof(int));
	}
	else { // OK
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
	}
}

// cl: local Unix socket, rcl: TLS socket
void client_stats(IDescriptor& cl, IDescriptor& rcl, request_type rqByteWithFlags) {
	// FIXME duplicated passthrough code
	// TODO when stats_multiple is implemented, need to switch on flags also here
	// in order to discriminate receiving a single path (file/folder) or a list of paths (multi stats)
	
	std::string dirpath = readStringWithLen(cl);

    BufferedWriteDescriptor brcl(rcl);
	brcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	writeStringWithLen(brcl,dirpath);
    brcl.flush();
	
	// read and pass-through response (OK + stats or error)
	uint8_t resp;
	int receivedErrno;
	rcl.readAllOrExit(&resp,sizeof(uint8_t));
	if (resp != 0) { // create error
		rcl.readAllOrExit(&receivedErrno,sizeof(int));
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
		cl.writeAllOrExit(&receivedErrno,sizeof(int));
	}
	else { // OK
		if (b0(rqByteWithFlags.flags)) {
			singleStats_resp_t sfstats{};
			readsingleStats_resp(rcl,sfstats);
			cl.writeAllOrExit(&resp,sizeof(uint8_t));
			writesingleStats_resp(cl,sfstats);
			return;
		}
		if (b1(rqByteWithFlags.flags)) {
			folderStats_resp_t fldstats{};
			readfolderStats_resp(rcl,fldstats);
			cl.writeAllOrExit(&resp,sizeof(uint8_t));
			writefolderStats_resp(cl,fldstats);
			return;
		}
		if (b2(rqByteWithFlags.flags)) {
			errno = 0x1234; // not yet implemented
			sendErrorResponse(cl);
			return;
		}
	}
}

// cl: local Unix socket, rcl: TLS socket
void client_hash(IDescriptor& cl, IDescriptor& rcl, request_type rqByteWithFlags) {
	uint8_t algorithm;
	uint8_t dirHashOpts;
	cl.readAllOrExit(&algorithm, sizeof(uint8_t));
	cl.readAllOrExit(&dirHashOpts, sizeof(uint8_t));
	PRINTUNIFIED("received algorithm hash position is:\t%u\n", algorithm);
	if (algorithm >= rh_hash_maxAlgoIndex) {
		sendErrorResponse(cl);
		return;
	}
    BufferedWriteDescriptor brcl(rcl);
	brcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	brcl.writeAllOrExit(&algorithm, sizeof(uint8_t));
	brcl.writeAllOrExit(&dirHashOpts, sizeof(uint8_t));

	std::string filepath = readStringWithLen(cl);
	PRINTUNIFIED("received filepath to hash is:\t%s\n", filepath.c_str());
	writeStringWithLen(brcl, filepath);
    brcl.flush();
	
	int errnum = receiveBaseResponse(rcl);
	if (errnum != 0) {
		PRINTUNIFIEDERROR("Error during remote hash computation");
		
		// do not use sendErrorResponse which uses local errno, propagate remote errno instead
		cl.writeAllOrExit(&RESPONSE_ERROR, sizeof(uint8_t));
        cl.writeAllOrExit(&errnum, sizeof(int));
		return;
	}
	std::vector<uint8_t> remoteHash(rh_hashSizes[algorithm],0);
	rcl.readAllOrExit(&remoteHash[0],rh_hashSizes[algorithm]);
	sendOkResponse(cl);
	cl.writeAllOrExit(&remoteHash[0],rh_hashSizes[algorithm]);
}

// cl: Unix socket, remoteCl: TLS socket wrapper
void client_ls(IDescriptor& cl, IDescriptor& rcl, const request_type rq) {
	std::string dirpath = readStringWithLen(cl);
	uint16_t dirpath_sz = dirpath.size();

	uint32_t totalRqSize = sizeof(uint8_t)+sizeof(uint16_t)+dirpath_sz;
	std::vector<uint8_t> ls_opt_rq(totalRqSize);
	uint8_t* v = &ls_opt_rq[0];
	memcpy(v,&rq,sizeof(uint8_t));
	memcpy(v+sizeof(uint8_t),&dirpath_sz,sizeof(uint16_t));
	memcpy(v+sizeof(uint8_t)+sizeof(uint16_t),dirpath.c_str(),dirpath_sz);
	rcl.writeAllOrExit(v,totalRqSize);
	
	// read and pass-through response
	
	uint8_t resp;
	int receivedErrno;
	rcl.readAllOrExit(&resp,sizeof(uint8_t));

	switch(resp) {
	    case RESPONSE_ERROR: // list dir error
	        rcl.readAllOrExit(&receivedErrno,sizeof(int));
            cl.writeAllOrExit(&resp,sizeof(uint8_t));
            cl.writeAllOrExit(&receivedErrno,sizeof(int));
            return;
        case RESPONSE_REDIRECT:
            dirpath = readStringWithLen(rcl); // receive and send redirect path
            cl.writeAllOrExit(&resp,sizeof(uint8_t));
            writeStringWithLen(cl,dirpath);
            break;
        case RESPONSE_OK:
            cl.writeAllOrExit(&resp,sizeof(uint8_t));
            break;
        default:
            PRINTUNIFIEDERROR("Unexpected response byte");
            threadExit();
	}
	
	if(rq.flags==2) { // read home path as response
		auto&& homepath = readStringWithLen(rcl);
		writeStringWithLen(cl,homepath);
		return;
	}
	
	for(;;) {
		ls_resp_t entry{};
		uint16_t len;
		rcl.readAllOrExit(&len, sizeof(uint16_t));
		if (len == 0) { // end of list indication
			cl.writeAllOrExit(&len, sizeof(uint16_t));
			break;
		}
		entry.filename.resize(len+1);

		rcl.readAllOrExit((char*)(entry.filename.c_str()), len);
		rcl.readAllOrExit(&(entry.date), sizeof(uint32_t));
		rcl.readAllOrExit(entry.permissions, 10);
		rcl.readAllOrExit(&(entry.size), sizeof(uint64_t));
		
		// writels_resp(cl, entry);
		cl.writeAllOrExit(&len, sizeof(uint16_t));
		cl.writeAllOrExit(entry.filename.c_str(), len);
		cl.writeAllOrExit(&(entry.date), sizeof(uint32_t));
		cl.writeAllOrExit(entry.permissions, 10);
		cl.writeAllOrExit(&(entry.size), sizeof(uint64_t));
	}
}

/**
 * Used by Android JNI side for allowing upload of files provided through a content uri by third-party apps,
 * by receiving the extracted file descriptors of those files over the Unix Domain Socket
 */
#ifndef _WIN32
void client_upload_from_fds(IDescriptor& cl, IDescriptor& rcl, const request_type rq) {
    PRINTUNIFIED("IN CLIENT UPLOAD FROM FDS\n");
    // receive total size (accumulated Java-side using content resolver query) for setting external progress
    uint64_t totalSize,totalFiles=0;
    cl.readAllOrExit(&totalSize,sizeof(uint64_t));

    auto&& progressHook = getProgressHook(totalSize);

    // send "client upload" request to server
    rcl.writeAllOrExit(&rq, sizeof(uint8_t));

    // send totals over remote socket as well (rh protocol modification for itaskbarlist progress)
    rcl.writeAllOrExit(&totalFiles,sizeof(uint64_t)); // unused, yet mandatory
    rcl.writeAllOrExit(&totalSize,sizeof(uint64_t));

    for(;;) {
        // receive destination path including desired filename and source file size (by Java), and file descriptor (by JNI)
        // here, we have an already open file descriptor to read from
        std::string destPath = readStringWithLen(cl);
        if(destPath.empty()) break;
        uint64_t size;
        cl.readAllOrExit(&size,sizeof(uint64_t));

        int receivedFd = recvfd((dynamic_cast<PosixDescriptor&>(cl)).desc);
        if(receivedFd < 0) {
            PRINTUNIFIEDERROR("Error receiving file descriptor over UDS\n");
            threadExit();
        }

        PosixDescriptor receivedFd_(receivedFd);
        OSUploadFromFileDescriptorWithProgress(receivedFd_,destPath,size,&cl,rcl,progressHook);

        cl.writeAllOrExit(&maxuint,sizeof(uint64_t));
        PRINTUNIFIED("End of file in caller\n");
    }

    // send end of files indicator to local descriptor
    cl.writeAllOrExit(&maxuint_2,sizeof(uint64_t));

    // send end of list to remote descriptor
    static constexpr uint8_t endOfList = 0xFF;
    rcl.writeAllOrExit(&endOfList,sizeof(uint8_t));
}
#endif

// FIXME mainly common code between client_upload and server_download, refactor with if statements (conditionally toggle different code blocks)
// client sending upload request and content (and sending progress indication back on cl unix socket)
// client UPLOADS to server
void client_upload(IDescriptor& cl, IDescriptor& rcl) {
    PRINTUNIFIED("IN CLIENT UPLOAD\n");
	// receive list of source-destination path pairs from cl
	std::vector<std::pair<std::string,std::string>> v = receivePathPairsList(cl);
	
	// count all files in the selection
	std::unordered_map<std::string,sts_sz> descendantCountMap;

	sts_sz counts{}; // no need to put this in the map

	for (auto& item : v) {
		sts_sz itemTotals = countTotalStatsIntoMap(item.first,descendantCountMap);
		counts.tFiles += itemTotals.tFiles;
		counts.tFolders += itemTotals.tFolders;
        counts.tSize += itemTotals.tSize;
	}

	auto&& progressHook = getProgressHook(counts.tSize);

    // No need for auto-flushing buffered descriptor here, genericUploadBasicRecursiveImplWithProgress and internal callees are already manually optimized
	// send "client upload" request to server
    auto x = static_cast<uint8_t>(ControlCodes::ACTION_UPLOAD);
	rcl.writeAllOrExit(&x, sizeof(uint8_t));

    // send counts.tFiles and counts.tSize to both rcl and cl
    for (auto* descriptor : {&rcl,&cl}) {
        // send counts.tFiles on local descriptor
        descriptor->writeAllOrExit(&(counts.tFiles),sizeof(uint64_t));
        // send total size as well
        descriptor->writeAllOrExit(&(counts.tSize),sizeof(uint64_t));
    }
	
	for (auto& item : v)
		genericUploadBasicRecursiveImplWithProgress(item.first,item.second,rcl,progressHook,&cl);
	
	// send end of files indicator to local descriptor
	cl.writeAllOrExit(&maxuint_2,sizeof(uint64_t));
	
	// send end of list to remote descriptor
	static constexpr uint8_t endOfList = 0xFF;
	rcl.writeAllOrExit(&endOfList,sizeof(uint8_t));
}

// client DOWNLOADS from server
void client_download(IDescriptor& cl, IDescriptor& rcl) {
	// receive list of source-destination path pairs from cl
	std::vector<std::pair<std::string,std::string>> v = receivePathPairsList(cl);
	
	// send "client download" request to server
    AutoFlushBufferedWriteDescriptor afrcl(rcl);
    auto x = static_cast<uint8_t>(ControlCodes::ACTION_DOWNLOAD);
    afrcl.writeAllOrExit(&x, sizeof(uint8_t));
	
	// send list of path pairs (files and maybe-not-empty folders)
	// receive back items with type flag, file full path, size and content, till list end
	sendPathPairsList(v,afrcl);
    afrcl.flush();
	
	downloadRemoteItems(rcl,&cl);
}

// ----------------@@@@@@@@@@@@@@@@@ rhss related logic and variable moved into xre.h

// avoids forking, just for debug purposes
void plainP7ZipSession(IDescriptor& cl, request_type rq) {
    switch (static_cast<ControlCodes>(rq.request)) {
        case ControlCodes::ACTION_COMPRESS:
            compressToArchive(cl,rq.flags);
            break;
        case ControlCodes::ACTION_EXTRACT:
            extractFromArchive(cl);
            break;
        default:
            PRINTUNIFIED("Unexpected request byte received\n");
    }
	threadExit();
}

// 1 request only, exit at the end, method to be invoked in fork
// only for p7zip operations, since signal handling wrapping in pthreads is uncomfortable
// fork/wait are called by a detached pthread, so the underlying client descriptor is not subject to concurrent access
void forkP7ZipSession(IDescriptor& cl, request_type rq) {
    // BEGIN DEBUG, AVOIDS FORKING
    //~ plainP7ZipSession(cl,rq);
    //~ return;
    // END DEBUG

	pid_t pid = fork();
	
	if (pid < 0) {
		PRINTUNIFIEDERROR("Unable to fork session for p7zip operation\n");
		sendErrorResponse(cl);
		return;
	}
	
	if (pid == 0) { // in child process
		
	try {
	switch (static_cast<ControlCodes>(rq.request)) {
	    case ControlCodes::ACTION_COMPRESS:
			compressToArchive(cl, rq.flags);
			break;
		case ControlCodes::ACTION_EXTRACT:
			extractFromArchive(cl);
			break;
		default:
			PRINTUNIFIED("Unexpected request byte received\n");
	}
	}
	catch (threadExitThrowable& i) {
		PRINTUNIFIEDERROR("fork7z_child... \n");
	}
	exit(0);
	}
	else { // in parent process
		int wstatus = 0;
		wait(&wstatus); // wait for child process termination in order to avoid this pthread go serve next requests while the long term process is active
	}
}

// this thread function is launched for serving REMOTE_STARTSERVER request
// on error, sends error back on cl unix socket and exits, else goes into event loop
/*
 * - No Unix Domain Socket needed after init: server doesn't need to show progress or communicate any information to the Java client
 * - During init Unix Domain Socket is needed just for communicating OK (server started) or error (no server thread started)
 * - No network socket, since it is this method that creates the network descriptor and binds to it, and spawns the other server threads for serving connected clients
 */

void on_server_acceptor_exit_func() {
	shutdown(rhss,SHUT_RDWR);
	close(rhss); // close communication with all remote endpoints
	close(rhss_local); // close communication with local endpoint (Java rhss update thread)
	rhss = -1;
	rhss_local = -1;
}

void forkServerAcceptor(int cl, uint8_t rq_flags) {
    PosixDescriptor pd_cl(cl);

    // LEGACY
//    // receive byte indicating whether to serve entire filesystem (0x00) or only custom directory (non-zero byte)
//	uint8_t x;
//	pd_cl.readAllOrExit(&x,sizeof(uint8_t));

    // NEW
    // receive strings for default path, announced path, exposed path (with default received empty means UNCHANGED from OS default)
    auto&& tmp = readStringWithLen(pd_cl);
    if(!tmp.empty()) currentXREHomePath = tmp;
    PRINTUNIFIED("xre home directory:\t%s\n", currentXREHomePath.c_str());
    xreAnnouncedPath = readStringWithLen(pd_cl);
    PRINTUNIFIED("received directory to be announced is:\t%s\n", xreAnnouncedPath.c_str());
    xreExposedDirectory = readStringWithLen(pd_cl);
    PRINTUNIFIED("received directory to be offered is:\t%s\n", xreExposedDirectory.c_str());
	
	// if server already active, bind will give error
	pid_t pid = fork();
	if (pid < 0) {
		PRINTUNIFIEDERROR("Unable to fork session for rh remote server\n");
		sendErrorResponse(pd_cl);
		return;
	}
	if (pid == 0) { // in child process (rh remote server acceptor)
		
		try {
		
		atexit(on_server_acceptor_exit_func);
		rhss = getServerSocket(cl);
		
		// from now on, server session threads communicate with local client over rhss_local
		rhss_local = cl;
		cl = -1;

		if(rq_flags == 5) {
			// start announce loop (for now with default parameters)
			std::thread announceThread(xre_announce);
            announceThread.detach();
		}

		acceptLoop(rhss);
		
		}
		catch (threadExitThrowable& i) {
			PRINTUNIFIEDERROR("forkXRE_child...\n");
		}
	}
	else { // in parent
        xreExposedDirectory.clear(); // this shouldn't be needed anymore
		rhss_pid = pid;
		// just exit current thread, don't need it anymore
		// cl descriptor is duplicated in parent and child, close it in parent
		// in order to avoid leaving connection open till parent exit
		pd_cl.close();
		threadExit();
	}
}

void killServerAcceptor(IDescriptor& cl) {
	if (rhss_pid > 0) {
		if (kill(rhss_pid,SIGINT) < 0) {
			sendErrorResponse(cl);
		}
		else {
			rhss_pid = -1;
			sendOkResponse(cl);
		}
	}
}

void getServerAcceptorStatus(IDescriptor& cl) {
	sendOkResponse(cl);
	if (rhss_pid > 0) { // running
		cl.writeAllOrExit(&RESPONSE_OK, sizeof(uint8_t));
	}
	else { // not running
		cl.writeAllOrExit(&RESPONSE_ERROR, sizeof(uint8_t));
	}
}

//void tlsClientSessionEventLoop(RingBuffer& inRb, Botan::TLS::Client& client, IDescriptor& cl) {
void tlsClientSessionEventLoop(TLS_Client& client_wrapper) {
  TLSDescriptor rcl(client_wrapper.inRb,*(client_wrapper.client));
  IDescriptor& cl = client_wrapper.local_sock_fd;
  try {
	PRINTUNIFIED("In TLS client event loop...\n");
	for(;;) {
		// read request from cl, propagate to rcl
		request_type rq{};
		cl.readAllOrExit(&rq,sizeof(rq));
		switch (static_cast<ControlCodes>(rq.request)) {
		    case ControlCodes::ACTION_LS:
				client_ls(cl,rcl,rq);
				break;
			case ControlCodes::ACTION_CREATE:
				client_createFileOrDirectory(cl,rcl,rq);
				break;
			case ControlCodes::ACTION_LINK:
				client_createHardOrSoftLink(cl,rcl,rq);
				break;
			case ControlCodes::ACTION_STATS:
				client_stats(cl,rcl,rq);
				break;
			case ControlCodes::ACTION_HASH:
				client_hash(cl,rcl,rq);
				break;
			case ControlCodes::ACTION_DOWNLOAD:
				client_download(cl,rcl);
				break;
			case ControlCodes::ACTION_UPLOAD:
                if (rq.flags == 0)
                    client_upload(cl,rcl);
                else
                    client_upload_from_fds(cl,rcl,rq);
				break;
			default:
				PRINTUNIFIEDERROR("Unexpected data received by client session thread from local socket, exiting thread...\n");
				threadExit();
		}
	}
  }
  catch (threadExitThrowable& i) {
    PRINTUNIFIEDERROR("T2 ...\n");
  }
  PRINTUNIFIEDERROR("[tlsClientSessionEventLoop] No housekeeping and return\n");
}

// only IPv4 addresses
void tlsClientSession(IDescriptor& cl) { // cl is local socket
	// receive server address
	std::string target = readStringWithByteLen(cl);
	
	// receive port
	uint16_t port;
	cl.readAllOrExit(&port,sizeof(uint16_t));
	
	PRINTUNIFIED("Received IP and port over local socket: %s %d\n",target.c_str(),port);

	auto&& remoteCl = netfactory.create(target, port);
    if(!remoteCl) {
        sendErrorResponse(cl);
        return;
    }

	PRINTUNIFIED("Remote client session connected to server %s, port %d\n",target.c_str(),port);
	sendOkResponse(cl); // OK, from now on java client can communicate with remote server using this local socket
	
	RingBuffer inRb;
	TLS_Client tlsClient(tlsClientSessionEventLoop,inRb,cl,remoteCl);
	tlsClient.go();
	
	// at the end, close the sockets
	remoteCl.close();
	cl.close();
}

void httpsUrlDownload(IDescriptor& cl, const uint8_t flags) { // cl is local socket
    // receive server address
    std::string target = readStringWithLen(cl);
    const bool downloadToFile = flags == 0; // flags: 000 -> download to file, 111 -> download to memory

    // receive port
    uint16_t port;
    cl.readAllOrExit(&port,sizeof(uint16_t));

    // receive destination directory
    std::string downloadPath = readStringWithLen(cl);

    // receive target filename, optional, ignore if empty string received
    std::string targetFilename = readStringWithLen(cl);

    PRINTUNIFIED("Received URL, port destination path and target filename over local socket:\n%s\n%d\n%s\n%s\n",
            target.c_str(),
            port,
            downloadPath.c_str(),
            targetFilename.empty()?"[No explicit filename provided]":targetFilename.c_str()
            );

    RingBuffer inRb;
    std::string redirectUrl;
    auto httpRet = httpsUrlDownload_internal(cl,target,port,downloadPath,targetFilename,inRb,redirectUrl,downloadToFile);
    
    // HTTP redirect limit
    for(int i=0;i<5;i++) {
        if(httpRet == 200) break;
        if(httpRet != 301 && httpRet != 302) {
            errno = httpRet;
            sendErrorResponse(cl);
            break;
        }
        inRb.reset();
        target = redirectUrl;
        httpRet = httpsUrlDownload_internal(cl,target,port,downloadPath,targetFilename,inRb,redirectUrl,downloadToFile);
    }

    // at the end, close the sockets
    cl.close();
}

// generate SSH keys in PKCS8 format via Botan
void ssh_keygen(IDescriptor& inOutDesc) {
	// TODO for now, only RSA supported, ignore the flag bits of rq and receive key size
	uint32_t keySize;
	inOutDesc.readAllOrExit(&keySize,sizeof(uint32_t));
	PRINTUNIFIED("Received key size: %u\n", keySize);
	auto&& keyPair = ssh_keygen_internal(keySize);
    sendOkResponse(inOutDesc); // actually not needed

    uint32_t prv_s_len = keyPair.first.size();
    uint32_t pub_s_len = keyPair.second.size();
    
    inOutDesc.writeAllOrExit(&prv_s_len,sizeof(uint32_t));
    inOutDesc.writeAllOrExit(keyPair.first.c_str(),prv_s_len);
    inOutDesc.writeAllOrExit(&pub_s_len,sizeof(uint32_t));
    inOutDesc.writeAllOrExit(keyPair.second.c_str(),pub_s_len);
}

// request types to be served in a new thread
void serveRequest(int intcl, request_type rq) {
	PosixDescriptor cl(intcl);
	
	switch (static_cast<ControlCodes>(rq.request)) {
	    case ControlCodes::ACTION_COMPRESS:
		case ControlCodes::ACTION_EXTRACT:
			forkP7ZipSession(cl,rq);
			break;
		case ControlCodes::ACTION_LS:
			listDirOrArchive(cl, rq.flags);
			break;
		case ControlCodes::ACTION_COPY:
			copyFileOrDirectoryFullNew(cl);
			break;
		case ControlCodes::ACTION_MOVE:
			moveFileOrDirectory(cl);
			break;
		case ControlCodes::ACTION_DELETE:
			deleteFile(cl);
			break;
		case ControlCodes::ACTION_STATS:
			stats(cl, rq.flags);
			break;
		case ControlCodes::ACTION_EXISTS:
			existsIsFileIsDir(cl, rq.flags);
			break;
		case ControlCodes::ACTION_CREATE:
			createFileOrDirectory(cl, rq.flags);
			break;
		case ControlCodes::ACTION_HASH:
			hashFile(cl);
			break;
		case ControlCodes::ACTION_FIND:
			try {
				findNamesAndContent(cl, rq.flags);
			}
			catch(...) {
				on_find_thread_exit_function();
				throw; // rethrow in order to exit from the for loop of 
			}
			break;
		case ControlCodes::ACTION_KILL:
			killProcess(cl);
			break;
		case ControlCodes::ACTION_GETPID:
			getThisPid(cl);
			break;
		//~ case ACTION_FORK:
			//~ forkNewRH(intcl,unixSocketFd);
			//~ break;
		case ControlCodes::ACTION_FILE_IO:
			readOrWriteFile(cl,rq.flags);
			break;
		//~ case ACTION_CANCEL:
			//~ cancelRunningOperations(intcl);
			//~ break;
			
		case ControlCodes::ACTION_SETATTRIBUTES:
			setAttributes(cl);
			break;
			
		case ControlCodes::ACTION_SSH_KEYGEN:
			ssh_keygen(cl);
			break;
		
		case ControlCodes::ACTION_LINK:
			createHardOrSoftLink(cl,rq.flags);
			break;
		
		case ControlCodes::REMOTE_SERVER_MANAGEMENT:
			switch(rq.flags) {
				case 0: // stop if active
					killServerAcceptor(cl);
					break;
				case 7: // 111 start
				case 5: // 101 NEW, start with IGMP announce loop
				    forkServerAcceptor(intcl,rq.flags);
                    threadExit(); // in order to avoid receiving further requests by this thread after fork
                    break;
				case 2: // 010 get status
					getServerAcceptorStatus(cl);
					break;
				default:
					errno = EINVAL; // bad request
					sendErrorResponse(cl);
			}
			break;
		
		case ControlCodes::REMOTE_CONNECT:
			// connect to rh remote server
			tlsClientSession(cl);
			break;
		// client disconnect: on process exit, or on Java client disconnect from local unix socket

        case ControlCodes::ACTION_HTTPS_URL_DOWNLOAD:
            httpsUrlDownload(cl, rq.flags);
            threadExit(); // no request continuation, one URL download per local thread
            break;
		default:
			PRINTUNIFIED("Unexpected request byte received\n");
			cl.close();
			threadExit();
      }
}

// serving requests for a single LOCAL client (no need for IDescriptor wrapping here)
void clientSession(int cl) {
	PosixDescriptor pd_cl(cl);
	try {
    for (;;) {
      // read request type (1 byte: 5 bits + 3 bits of flags)
      request_type rq{};
      pd_cl.readAllOrExit(&rq, sizeof(rq));
      
      PRINTUNIFIED("request 5-bits received:\t%u\n", rq.request);
      PRINTUNIFIED("request flags (3-bits) received:\t%u\n", rq.flags);
      
      if (static_cast<ControlCodes>(rq.request) == ControlCodes::ACTION_EXIT) {
		  PRINTUNIFIED("Received exit request, exiting...\n");
		  exit(0);
	  }
      else {
		  serveRequest(cl,rq);
	  }
    }
    
	}
	catch (threadExitThrowable& i) {
		PRINTUNIFIED("RH2...\n");
	}
    pd_cl.close();
    PRINTUNIFIED("disconnected\n");
}

void exitRhss() {
	// rhss_pid default value already set to non-wildcard, non-valid pid, this check is redundant
	if (rhss_pid > 0) kill(rhss_pid,SIGINT);
}

void rhMain(int uid=rh_default_uid, std::string name=rh_uds_default_name) {
	NT_CHECK
  
	// roothelper valid_euid socket_name
	CALLER_EUID = uid;
	PRINTUNIFIED("Running in authenticated mode, valid euid: %d\n",CALLER_EUID);
	strcpy(SOCKET_ADDR,name.c_str()); // custom socket name (for on-demand spawn roothelpers, in case of long-term operations)
	PRINTUNIFIED("Running on socket name: %s\n",name.c_str());


#ifdef ANDROID_NDK
	strcat(LOG_TAG_WITH_SOCKET_ADDR,PROGRAM_NAME);
	strcat(LOG_TAG_WITH_SOCKET_ADDR+strlen(PROGRAM_NAME)," ");
	strcat(LOG_TAG_WITH_SOCKET_ADDR+strlen(PROGRAM_NAME)+1,SOCKET_ADDR);
#endif

	// TODO Remember that also JNI wrapper needs to call this!
    if (!lib.Load(NDLL::GetModuleDirPrefix() + FTEXT(kDllName)))
        PRINTUNIFIEDERROR("Can not load p7zip library, list archive, compress and extract operations won't be available\n");
    else lib7zLoaded = true;

    // Main code of PGP's rootHelper
	signal(SIGPIPE,SIG_IGN);
	atexit(exitRhss);
	
	struct sockaddr_un addr;
	socklen_t len = 0;
	
	int unixSocketFd = getServerUnixDomainSocket(addr,len,SOCKET_ADDR);
	if(unixSocketFd < 0) _Exit(-1);
	
	for (;;) {
		PRINTUNIFIED("waiting for client connection...");
		fflush(stdout);
        int cl;
		if ((cl = accept(unixSocketFd, (struct sockaddr *)(&addr), &len)) == -1) {
			PRINTUNIFIEDERROR("accept error");
			continue;
		}
		PRINTUNIFIED("accept ok\n");
		
		// authenticate peer
		// invoke from Android with second argument as Binder.getCallingUid()
		
		if (!authenticatePeer(cl)) {
			close(cl);
			PRINTUNIFIED("authentication failed, disconnecting client...\n");
			continue;
		}
		// here pass control to request handler //
		std::thread t(clientSession,cl);
		t.detach();
	}
}

////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
// MERGED MAIN WITH ARGS SWITCH
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////
////////////////////////////////////////////////

int MY_CDECL main(int argc, const char *argv[]) {
	initLogging();
	initDefaultHomePaths();
	registerExitRoutines();
	print_roothelper_version();
	if (prog_is_xre(argv[0])) {
		if(argc >= 2 && mode_is_help(argv[1])) {
			print_help(argv[0]);
		}
		else {
			PRINTUNIFIED("xre mode by filename\n");
			return xreMain(argc,argv,getSystemPathSeparator());
		}
	}
	else if (argc >= 2) {
	    std::string arg1 = TOUTF(argv[1]);
	    if(allowedFromCli.find(arg1) != allowedFromCli.end()) {
            PRINTUNIFIED("cli mode\n");
	        return allowedFromCli.at(arg1).second(argc,argv);
	    }
		if (mode_is_xre(arg1)) {
			PRINTUNIFIED("xre mode\n");
			return xreMain(argc,argv,getSystemPathSeparator());
		}
		else if(mode_is_help(arg1)) {
			print_help(argv[0]);
		}
		else { // capture second argument as UID, third as socket name
			PRINTUNIFIED("rh mode\n");
			int uid = std::atoi(argv[1]);
			std::string name = rh_uds_default_name;
			if (argc == 3) {
				name = argv[2];
			}
			rhMain(uid,name);
		}
	}
	else { // argc == 1, rh mode, default arguments
		PRINTUNIFIED("rh mode, defaults\n");
		rhMain();
	}
	return 0;
}
