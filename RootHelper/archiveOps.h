#ifndef __RH_ARCHIVE_OPS_H__
#define __RH_ARCHIVE_OPS_H__

#include "archiveUtils.h"

inline bool has_suffix(const std::string &str, const std::string &suffix) {
    return str.size() >= suffix.size() &&
           str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
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

// for GZ,BZ2,XZ archives (that contains only one file stream with no attributes)
// do not even check header, will be checked on extract, just return name without extension
void listStreamArchive(IDescriptor& inOutDesc, const std::string& archiveName, ArchiveType archiveType) {
    inOutDesc.writeAllOrExit( &RESPONSE_OK, sizeof(uint8_t));

    ls_resp_t responseEntry{};
    responseEntry.filename = getVirtualInnerNameForStreamArchive(archiveName, archiveType);
    memset(responseEntry.permissions, '-', 10);

    writeLsRespOrExit(inOutDesc,responseEntry);

    uint16_t terminationLen = 0;
    inOutDesc.writeAllOrExit( &terminationLen, sizeof(uint16_t));
}

// return value when inOutDesc is null:
// -  1: there is a single item in the archive root 
// -  2: more than one item in the archive root
// - -1: there were errors (generic)
// - -2: errors during archive type detection
// return value is ignored if inOutDesc is non-null
int listArchiveInternalOrCheckForSingleItem(const std::string& archivepath, const std::string& password, IDescriptor* inOutDesc = nullptr) {
    if(!lib7zLoaded) {
        errno = 771;
        if(inOutDesc) sendErrorResponse(*inOutDesc);
        return -1;
    }
    auto createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
    if (!createObjectFunc) {
        PrintError("Can not get CreateObject");
        _Exit(-1);
    }
	
	// perform list archive
    auto&& archivepath_ = UTF8_to_wchar(archivepath);
    FString archiveName(archivepath_.c_str());
    
    // archive type detection
    ArchiveType archiveType = detectArchiveType(archivepath);
    // PRINTUNIFIED("Archive type is: %u\n",archiveType);
    if (archiveType == UNKNOWN) {
        PrintError("Unable to open archive with any associated codec", archiveName);
        errno = 23458;
        if(inOutDesc) sendErrorResponse(*inOutDesc);
        return -2;
    }

    // for stream archives, just send a virtual inner name without actually opening the archive
    switch (archiveType) {
        case XZ:
        case GZ:
        case BZ2:
            if(inOutDesc)
                listStreamArchive(*inOutDesc,getFilenameFromFullPath(archivepath),archiveType);
            return 1; // streaming archives contain only one file by construction
        default:
            break;
    }

    CMyComPtr<IInArchive> archive;

    if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IInArchive, (void **)&archive) != S_OK) {
        PrintError("Can not get class object");
        errno = 23459;
        if(inOutDesc) sendErrorResponse(*inOutDesc);
        return -1;
    }

    //////////////////////////////////////

    auto fileSpec = new CInFileStream;
    CMyComPtr<IInStream> file = fileSpec;

    if (!fileSpec->Open(archiveName)) {
        PrintError("Can not open archive file", archiveName);
        errno = EACCES; // simulate access error to file in errno
        if(inOutDesc) sendErrorResponse(*inOutDesc);
        return -1;
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
            if(inOutDesc) sendErrorResponse(*inOutDesc);
            return -1;
        }
    }
	
	    // Enumerate and send archive entries if inOutDesc is non-null
    UInt32 numItems = 0;
    archive->GetNumberOfItems(&numItems);

    PRINTUNIFIED("Entering entries enumeration block, size is %u\n", numItems); // DEBUG

    if(inOutDesc) inOutDesc->writeAllOrExit(&RESPONSE_OK, sizeof(uint8_t));
	
	std::string currentTopItem; 

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

                if(responseEntry.filename.empty()) {
                    // filename as empty string not allowed, interpreted as EOL by rh client
                    PRINTUNIFIEDERROR("Empty filename in archive listing, ignoring it\n");
                    continue;
                }
                else PRINTUNIFIED("entry name: %s\n", responseEntry.filename.c_str()); // DEBUG
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

        if(inOutDesc) writeLsRespOrExit(*inOutDesc,responseEntry);
		else {
			auto idx = responseEntry.filename.find('/'); // FIXME not sure if 7zip uses / (unix style) or \ (windows style)
			if(currentTopItem.empty()) {
				currentTopItem = (idx == std::string::npos) ? responseEntry.filename : responseEntry.filename.substr(0,idx); // truncate before first path separator if any
			}
			else {
				// TODO truncate before first '/' if any, and compare with currentTopItem; if changed, return 2;
				auto newTopItem = (idx == std::string::npos) ? responseEntry.filename : responseEntry.filename.substr(0,idx); // truncate before first path separator if any
				if(newTopItem != currentTopItem)
					return 2; // archive contains more than 1 item at the root level
			}
		}
    }

    // list termination indicator
    uint16_t terminationLen = 0;
    if(inOutDesc) inOutDesc->writeAllOrExit(&terminationLen, sizeof(uint16_t));
	return 1;
}

// list compressed archive returning all entries (at every archive directory tree level) as relative-to-archive paths
// error code is not indicative in errors generated in this function, errno is set manually before calling sendErrorResponse
// only UTF-8 codepoint 0 contains the \0 byte (unlike UTF-16)
// web source: http://stackoverflow.com/questions/6907297/can-utf-8-contain-zero-byte
void listArchive(IDescriptor& inOutDesc, const uint8_t flags) {
    // read input from socket (except first byte - action code - which has already been read by caller, in order to perform action dispatching)
    std::string archivepath = readStringWithLen(inOutDesc);

    // read password, if provided
    std::string password = readStringWithByteLen(inOutDesc);
    
    if (flags == 7) // flags 111
		listArchiveInternalOrCheckForSingleItem(archivepath, password, &inOutDesc);
    else { // flags 110
		int ret = listArchiveInternalOrCheckForSingleItem(archivepath, password);
		if(ret <= 0) sendErrorResponse(inOutDesc); // errno already set in listArchiveInternalOrCheckForSingleItem
		else {
			sendOkResponse(inOutDesc);
			uint64_t ret_ = ret;
			inOutDesc.writeAllOrExit(&ret_,sizeof(uint64_t));
		}
	}
}

/*
 - on extract, p7zip takes as input a list of indexes to be extracted (they correspond to nodes without children,
that is, files or folders themselves without their sub-tree nodes)
- XFiles UI allows to choose some items at a subdirectory level in an archive, but if an item is a directory, XFiles sends
 all the entries of its subtree (the entire archive content tree has already been stored in a VMap on archive open for listing)
- if items are not top-level in the archive, the path truncation offset is needed in order to perform natural relative extraction

if flags == 6 (binary: 110), internally call list archive with the same flag in order to determine whether to create or not an intermediate directory
 */
void extractFromArchive(IDescriptor& inOutDesc, const uint8_t flags)
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

    bool smartCreateDirectory = (flags == 6) && (nOfEntriesToExtract == 0);

    std::vector<uint32_t> currentEntries;

    PRINTUNIFIED("nOfEntriesToExtract: %d\n", nOfEntriesToExtract);
    uint32_t subDirLengthForPathTruncateInWideChars = 0;
    if (nOfEntriesToExtract != 0)
    {
        // DO NOT RECEIVE FILENAME ENTRIES, RECEIVE INDICES INSTEAD - TODO check if works with all archive types
        // smart directory creation option does not apply here
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

    if(smartCreateDirectory) {
        int ret = listArchiveInternalOrCheckForSingleItem(srcArchive,password);
        if(ret != 1 && ret != 2) {
            errno = EINVAL;
            PRINTUNIFIEDERROR("Unable to determine whether the archive contains one or more items at the top level\n");
            sendErrorResponse(inOutDesc);
            return;
        }

        if(ret == 2) {
            // update destination folder by adding a subfolder with archive name
            std::string tmp;
            auto ret = getFileExtension(srcArchive,tmp,true);
            destFolder += "/";
            destFolder += ((ret == 0) ? tmp : std::string("intermediate"));
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
    int extRet = getFileExtension(destArchive,outExt,false);
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
                    (UInt32)(compress_options.compressionLevel),    // compression level
                    (compress_options.solid > 0),                    // solid mode
                    (compress_options.encryptHeader > 0)            // encrypt filenames
            };

    NWindows::NCOM::CPropVariant values_other[1]
            {
                    (UInt32)(compress_options.compressionLevel)    // compression level
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

#endif /* __RH_ARCHIVE_OPS_H__ */
