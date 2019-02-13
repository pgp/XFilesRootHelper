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

#include "tls/botan_rh_rserver.h"
#include "tls/botan_rh_rclient.h"

#include "resps/singleStats_resp.h"
#include "resps/folderStats_resp.h"

#include "FileCopier.h"

#ifndef _WIN32
#include "desc/PosixDescriptor.h"
#include "androidjni/wrapper.h"
#include "archiveUtils.h"
#include "args_switch.h"
#endif

#include "xre.h"

#define PROGRAM_NAME "roothelper"
char SOCKET_ADDR[32]={};
int CALLER_EUID = -1; // populated by second command-line argument, in order to verify client credentials (euid must match on connect)

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
  if (B0(flags))
  { // B0: check for existence
    PRINTUNIFIED("Checking existence...\n");
    if (access(filepath.c_str(), F_OK) == 0)
      SETB0(respFlags, 1); // set bit 0 if file exists
    else goto response; // all to zero if path not exists
  }
  stat(filepath.c_str(), &st);
  if (B1(flags))
  { // B0: check if is file (false even if not exists)
    PRINTUNIFIED("Checking \"is file\"...\n");
    if (S_ISREG(st.st_mode))
      SETB1(respFlags, 1);
  }
  if (B2(flags))
  { // B0: check if it is folder (false even if not exists)
    PRINTUNIFIED("Checking \"is dir\"...\n");
    if (S_ISDIR(st.st_mode))
      SETB2(respFlags, 1);
  }

// response byte unconditionally 0x00 (OK), plus one byte containing the three bit flags set accordingly:
// B0: true if file or dir exists
// B1: true if is file
// B2: true if is dir
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
				if (mkpathCopyPermissionsFromNearestAncestor(parent.c_str()) != 0)
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
void moveFileOrDirectory(IDescriptor& inOutDesc, uint8_t flags)
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

void copyFileOrDirectoryFullNew(IDescriptor& inOutDesc, uint8_t _unusedFlags_ = 0) {
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


// segfaults on the third invocation
// not really a problem, since compress task is forked into a new process
// compress or update (TODO) if the destination archive already exists
void compressToArchive(IDescriptor& inOutDesc, uint8_t flags) {
  Func_CreateObject createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
  if (!createObjectFunc) {
    PrintError("Can not get CreateObject");
    exit(-1);
  }

  // BEGIN RECEIVE DATA: srcFolder, destArchive, filenames in srcFolder to compress
  
  std::vector<std::string> srcDestPaths = readPairOfStringsWithPairOfLens(inOutDesc);
  std::string srcFolder = srcDestPaths[0];
  std::string destArchive = srcDestPaths[1];
  PRINTUNIFIED("received source folder path is:\t%s\n", srcFolder.c_str());
  PRINTUNIFIED("received destination archive path is:\t%s\n", destArchive.c_str());
  
  FString archiveName(UTF8_to_wchar(destArchive.c_str()).c_str());

  // receive compress options
  compress_rq_options_t compress_options = {};
  readcompress_rq_options(inOutDesc,compress_options);
  PRINTUNIFIED("received compress options:\tlevel %u, encryptHeader %s, solid %s\n",
				compress_options.compressionLevel,
				compress_options.encryptHeader?"true":"false",
				compress_options.solid?"true":"false");
  
  // read password, if provided
  std::string password = readStringWithByteLen(inOutDesc);
  if (password.empty()) PRINTUNIFIED("No password provided, archive will not be encrypted\n");

  // receive number of filenames to be received, 0 means compress entire folder content
  uint32_t nOfItemsToCompress, i;
  inOutDesc.readAllOrExit( &(nOfItemsToCompress), sizeof(uint32_t));
  
  std::vector<std::string> currentEntries; // actually, this will contain only filenames, not full paths
  
  PRINTUNIFIED("Number of items to compress is %u\n",nOfItemsToCompress);

  if (nOfItemsToCompress != 0) {
	currentEntries.reserve(nOfItemsToCompress);
    for (i = 0; i < nOfItemsToCompress; i++)
      currentEntries.push_back(readStringWithLen(inOutDesc));
  }
  // END RECEIVE DATA
  
  // change working directory to srcFolder
  char currentDirectory[1024] = {};
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
    int i;
    std::unique_ptr<IDirIterator<std::string>> dirIt;

    if (nOfItemsToCompress == 0) {
      dirIt = itf.createIterator(srcFolder.c_str(), RELATIVE_WITHOUT_BASE, false);
      while (dirIt->next())
      {
		std::string curEntString = dirIt->getCurrent();

		CDirItem di;
		FString name = CmdStringToFString(curEntString.c_str());
		std::wstring uws = UTF8_to_wchar(curEntString.c_str());
		UString u(uws.c_str());

        NFind::CFileInfo fi;
        if (!fi.Find(name))
        {
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
      for (i = 0; i < nOfItemsToCompress; i++)
      {
        // stat current item, if it's a directory, open a stdfsIterator over it with parent prepend mode
        struct stat st;
        stat(currentEntries[i].c_str(), &st); // filepath is relative path (filename only)
        if (S_ISDIR(st.st_mode))
        {
          // STDFSITERATOR ONLY ALLOWS ABSOLUTE PATHS
          std::stringstream ss;
          ss<<srcFolder<<"/"<<currentEntries[i];
          dirIt = itf.createIterator(ss.str(), RELATIVE_INCL_BASE, false);
          while (dirIt->next())
          {
            std::string curEntString = dirIt->getCurrent();
            PRINTUNIFIED("Current item: %s\n",curEntString.c_str());

            CDirItem di;
            
            FString name = CmdStringToFString(curEntString.c_str());
			std::wstring uws = UTF8_to_wchar(curEntString.c_str());
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
			std::wstring uws = UTF8_to_wchar(currentEntries[i].c_str());
			UString u(uws.c_str());
			
			NFind::CFileInfo fi;
			if (!fi.Find(name))
			{
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

  ///////////////////////////////////////////////////////////////////////////
  
  // check existence of destination archive's parent directory
  // if not existing, create it, if creation fails send error message
  std::string destParentDir = getParentDir(destArchive);
  if (mkpathCopyPermissionsFromNearestAncestor(destParentDir.c_str()) != 0) {
	  PRINTUNIFIEDERROR("Unable to create destination archive's parent directory\n");
	  sendErrorResponse(inOutDesc);
	  return;
  }

  COutFileStream *outFileStreamSpec = new COutFileStream;
  CMyComPtr<IOutStream> outFileStream = outFileStreamSpec;
  if (!outFileStreamSpec->Create(archiveName, false))
  {
    PrintError("can't create archive file");
    // errno = EACCES; // or EEXIST // errno should already be set
    sendErrorResponse(inOutDesc);
    return;
  }

  CMyComPtr<IOutArchive> outArchive;
  // get extension of dest file in order to choose archive encoder, return error on not supported extension
  // only 7Z,ZIP and TAR for now
  std::string outExt;
  ArchiveType archiveType = UNKNOWN;
  int extRet = getFileExtension(destArchive,outExt);
  if (extRet < 0) goto unknownOutType;
  
  if (outExt.compare("7z") == 0) {
	  archiveType = _7Z;
	  
  }
  else if (outExt.compare("zip") == 0) {
	  archiveType = ZIP;
  }
  else if (outExt.compare("tar") == 0) {
	  archiveType = TAR;
  }
  
  unknownOutType:
  if (archiveType == UNKNOWN) {
	  PrintError("Unable to find any codec associated to the output archive extension for the pathname ", archiveName);
      errno = 23458;
      sendErrorResponse(inOutDesc);
      return;
  }
  
  if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IOutArchive, (void **)&outArchive) != S_OK)
  {
    PrintError("Can not get class object");
    errno = 12344;
    sendErrorResponse(inOutDesc);
    return;
  }

  CArchiveUpdateCallback *updateCallbackSpec = new CArchiveUpdateCallback;
  
  updateCallbackSpec->setDesc(&inOutDesc); // for publishing progress
  
  CMyComPtr<IArchiveUpdateCallback2> updateCallback(updateCallbackSpec);
  
  // TAR does not support password-protected archives, however setting password
  // doesn't result in error (archive is simply not encrypted anyway), so no need
  // to add explicit input parameter check
  updateCallbackSpec->PasswordIsDefined = (!password.empty());
  updateCallbackSpec->Password = FString(UTF8_to_wchar(password.c_str()).c_str());

  updateCallbackSpec->Init(&dirItems);
  
  // ARCHIVE OPTIONS
  // independently from what options are passed to roothelper, the ones
  // which are not-compatible with the target archive format are ignored
  
  // names
  const wchar_t *names_7z[] =
  {
        L"x", // compression level
        L"s", // solid mode
        L"he" // encrypt filenames
  };
  
  const wchar_t *names_zip[] =
  {
        L"x", // compression level
  };
  
  // values
  NWindows::NCOM::CPropVariant values_7z[3] =
  {
        (UInt32)(compress_options.compressionLevel),	// compression level
        (compress_options.solid > 0),					// solid mode
        (compress_options.encryptHeader > 0)			// encrypt filenames
  };
  
  NWindows::NCOM::CPropVariant values_zip[1] =
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
		return;
      }
      int setPropertiesResult;
      switch (archiveType) {
		  case _7Z:
			setPropertiesResult = setProperties->SetProperties(names_7z, values_7z, 3); // last param: kNumProps
			break;
		  case ZIP:
			setPropertiesResult = setProperties->SetProperties(names_zip, values_zip, 1);
			break;
		  default:
			break; // do not set any option for TAR and other types
	  }
      
      if(setPropertiesResult) {
		PrintError("Unable to setProperties for archive");
        errno = 12378;
		sendErrorResponse(inOutDesc);
		return;
	  }
  // END ARCHIVE OPTIONS
  
  sendOkResponse(inOutDesc); // means: archive init OK, from now on start compressing and publishing progress

  HRESULT result = outArchive->UpdateItems(outFileStream, dirItems.Size(), updateCallback);

  updateCallbackSpec->Finilize();
  
  if (result != S_OK)
  {
    PrintError("Update Error");
    errno = 12345;
    sendEndProgressAndErrorResponse(inOutDesc);
    return;
  }

  // unreachable in case or error
  FOR_VECTOR(i, updateCallbackSpec->FailedFiles)
  {
    PrintNewLine();
    PrintError("Error for file", updateCallbackSpec->FailedFiles[i]);
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

std::string getVirtualInnerNameForStreamArchive(std::string archiveName, ArchiveType archiveType) {
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
	Func_CreateObject createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
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
	
	FString archiveName(UTF8_to_wchar(srcArchive.c_str()).c_str());
	
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
  

	CInFileStream *fileSpec = new CInFileStream;
	CMyComPtr<IInStream> file = fileSpec;

	if (!fileSpec->Open(archiveName)) {
		PrintError("Can not open archive file", archiveName);
		errno = EACCES; // simulate access error to file in errno
		sendErrorResponse(inOutDesc);
		return;
	}

	{
    CArchiveOpenCallback *openCallbackSpec = new CArchiveOpenCallback;
    CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
    openCallbackSpec->PasswordIsDefined = (!password.empty());
    openCallbackSpec->Password = FString(UTF8_to_wchar(password.c_str()).c_str());;

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
  CArchiveExtractCallback *extractCallbackSpec = new CArchiveExtractCallback;
  // generate virtual single-child name by archive name in case of stream archive types
  if (archiveType == XZ || archiveType == GZ || archiveType == BZ2)
	extractCallbackSpec->streamArchiveOnlyChildFilenameOnly = UTF8_to_wchar(
																getVirtualInnerNameForStreamArchive(
																	getFilenameFromFullPath(srcArchive),
																archiveType).c_str());
  
  extractCallbackSpec->setDesc(&inOutDesc); // for publishing progress
  extractCallbackSpec->setSubDirLengthForPathTruncate(subDirLengthForPathTruncateInWideChars);
  
  CMyComPtr<IArchiveExtractCallback> extractCallback(extractCallbackSpec);
  
  sendOkResponse(inOutDesc); // means: archive init OK, from now on start extracting and publishing progress
  
  extractCallbackSpec->Init(archive, FString(UTF8_to_wchar(destFolder.c_str()).c_str()));
  extractCallbackSpec->PasswordIsDefined = (!password.empty());
  extractCallbackSpec->Password = FString(UTF8_to_wchar(password.c_str()).c_str());

  HRESULT result;

  if (currentEntries.size() == 0)
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
void listStreamArchive(IDescriptor& inOutDesc, std::string archiveName, ArchiveType archiveType) {
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
	Func_CreateObject createObjectFunc = (Func_CreateObject)lib.GetProc("CreateObject");
	if (!createObjectFunc) {
		PrintError("Can not get CreateObject");
		exit(-1);
	}
	
	// read input from socket (except first byte - action code - which has already been read by caller, in order to perform action dispatching)
	std::string archivepath = readStringWithLen(inOutDesc);
	
	// read password, if provided
	std::string password = readStringWithByteLen(inOutDesc);
	
	// perform list archive
	FString archiveName(UTF8_to_wchar(archivepath.c_str()).c_str());
	
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
	}
	
	if (createObjectFunc(&(archiveGUIDs[archiveType]), &IID_IInArchive, (void **)&archive) != S_OK) {
	PrintError("Can not get class object");
	errno = 23459;
	sendErrorResponse(inOutDesc);
	return;
	}

	//////////////////////////////////////
	
	CInFileStream *fileSpec = new CInFileStream;
	CMyComPtr<IInStream> file = fileSpec;
	
	if (!fileSpec->Open(archiveName)) {
		PrintError("Can not open archive file", archiveName);
		errno = EACCES; // simulate access error to file in errno
		sendErrorResponse(inOutDesc);
		return;
	}
	
	{
		CArchiveOpenCallback *openCallbackSpec = new CArchiveOpenCallback;
		CMyComPtr<IArchiveOpenCallback> openCallback(openCallbackSpec);
		
		openCallbackSpec->PasswordIsDefined = (!password.empty());
		openCallbackSpec->Password = FString(UTF8_to_wchar(password.c_str()).c_str());

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
		char tmp1[1024] = {};
		{
			// Get uncompressed size of file
			NCOM::CPropVariant prop;
			archive->GetProperty(i, kpidSize, &prop);
			char s[32] = {};
			ConvertPropVariantToShortString(prop, s);
			responseEntry.size = atol(s);
		}
    //~ {
		//~ // Get compressed size of file (TO BE TESTED)
		//~ NCOM::CPropVariant prop;
		//~ archive->GetProperty(i, kpidPackSize, &prop);
		//~ char s[32] = {};
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
	if (flags == 0) { // all bits set or none (for simplicity, since we need only one flag bit at the current time)
		listDir(inOutDesc);
	}
	else {
		PosixDescriptor pd = static_cast<PosixDescriptor&>(inOutDesc);
		listArchive(pd);
	}
}

const char* find_legend = R"V0G0N(
/*
 * FIND request:
 *  - Input: source path (dir or archive), filename pattern (can be empty), content pattern (can be empty)
 *  - Flag bits:
 * 				B0 - (Overrides other bits) Cancel current search, if any
 * 				B1 - Search in subfolders only (if false, search recursively)
 * 				B2 - Search in archives (search within archives if true) - CURRENTLY IGNORED
 *  - Two additional bytes for pattern options:
 * 				- For filename pattern:
 * 				B0 - Use regex (includes escaped characters, case-sensitivity options and whole word options) - CURRENTLY IGNORED
 * 				B1 - Use escaped characters (if B0 true, B1 is ignored) - CURRENTLY IGNORED
 * 				B2 - Case-insensitive search if true (if B0 true, B2 is ignored)
 * 				B3 - Whole-word search (if B0 true, B3 is ignored) - CURRENTLY IGNORED
 * 				- For content pattern:
 * 				B4,B5,B6,B7 same as B0,B1,B2,B3
 * 				B8 (B0 of second byte): find all occurrences in content
 */
)V0G0N";

// std::atomic_int currentSearchInOutDesc(-1); // at most one search thread per roothelper instance
std::mutex currentSearchInOutDesc_mx;
bool searchInterrupted = false; // FIXME not really thread-safe, replace with more robust mechanism
IDescriptor* currentSearchInOutDesc = nullptr; // at most one search thread per roothelper instance

void on_find_thread_exit_function() {
	PRINTUNIFIED("Resetting find thread global descriptor\n");
	// currentSearchInOutDesc.store(-1);
	std::unique_lock<std::mutex> lock(currentSearchInOutDesc_mx);
	currentSearchInOutDesc->close();
	delete currentSearchInOutDesc;
	currentSearchInOutDesc = nullptr;
}

void findNamesAndContent(IDescriptor& inOutDesc, uint8_t flags) {
	// BEGIN DEBUG
	// print rq flags
	PRINTUNIFIED("Find rq flags:\n");
	PRINTUNIFIED("B0 %d\n",B0(flags));
	PRINTUNIFIED("B1 %d\n",B1(flags));
	PRINTUNIFIED("B2 %d\n",B2(flags));
	// END DEBUG
	
	std::unique_lock<std::mutex> lock(currentSearchInOutDesc_mx);
	if (B0(flags)) { // cancel current search, if any
		searchInterrupted = true;
		currentSearchInOutDesc->close(); // will cause search thread, if any, to exit on socket write error, in so freeing the allocated global IDescriptor
		lock.unlock();
		
		// close(currentSearchInOutDesc.load()); // will cause search thread, if any, to exit on socket write error
		// currentSearchInOutDesc.store(-1);
		sendOkResponse(inOutDesc);
		return;
	}
	
	// if (currentSearchInOutDesc.load() > 0) { // another search task already active, and we are trying to start a new one
	if (currentSearchInOutDesc != nullptr) { // another search task already active, and we are trying to start a new one
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
	
	// on_thread_exit_generic(on_find_thread_exit_function);
	auto fpd = dynamic_cast<PosixDescriptor&>(inOutDesc);

	//~ currentSearchInOutDesc.store(inOutDesc); // here atomic compare and set would be needed, or at least volatile variable
	
	currentSearchInOutDesc = new PosixDescriptor(fpd.desc);
	lock.unlock();
	
	sendOkResponse(inOutDesc);
	searchInterrupted = false;
	
	// check flags
	if (B1(flags)) { // search in base folder only
		PRINTUNIFIED("Entering plain listing...\n");
		DIR *d;
		struct dirent *dir;
		d = opendir(basepath.c_str());
		if (d) {
			uint16_t filenamelen;
			while ((dir = readdir(d)) != nullptr) {
				// exclude current (.) and parent (..) directory
				if (strcmp(dir->d_name, ".") == 0 ||
					strcmp(dir->d_name, "..") == 0) continue;
					
				if (searchInterrupted) {
					PRINTUNIFIEDERROR("Search interrupted,exiting...\n");
					threadExit();
				}
				
				std::string haystack = std::string(dir->d_name);
				std::string needle = namepattern;
				if (B2(searchFlags)) {
					toUppercaseString(haystack);
					toUppercaseString(needle);
				}
				if (haystack.find(needle) != std::string::npos) {
					find_resp_t findEntry{};
					
					std::string filepathname = pathConcat(basepath.c_str(),dir->d_name);
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
		// std::unique_ptr<IDirIterator<std::string>> dirIt = itf.createIterator(basepath.c_str(), FULL, true, RECURSIVE);
		std::unique_ptr<IDirIterator<std::string>> dirIt = itf.createIterator(basepath.c_str(), FULL, true, SMART_SYMLINK_RESOLUTION);
		while (dirIt->next()) {
			std::string curEntString = dirIt->getCurrent();
			std::string curEntName = dirIt->getCurrentFilename();
			
			if (searchInterrupted) {
				PRINTUNIFIEDERROR("Search interrupted,exiting...\n");
				threadExit();
			}
			
			std::string haystack = curEntName;
			std::string needle = namepattern;
			if (B2(searchFlags)) {
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
}

#ifdef __linux__
bool authenticatePeer(int nativeDesc) {
  struct ucred cred; // members: pid_t pid, uid_t uid, gid_t gid
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
	int err = 0;
    
	int readBytes,writtenBytes;
	if (flags) { // read from file, send to client
		// UNATTENDED READ: at the end of the file close connection
		PRINTUNIFIED("Read from file, send to client\n");
		struct stat st = {};
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
		
		
		std::unique_ptr<IDescriptor> fd = fdfactory.create(filepath,"rb",err);
		if (err != 0) {
			sendErrorResponse(inOutDesc);
			return;
		}
		
		sendOkResponse(inOutDesc);
		for(;;) {
			readBytes = fd->read(&iobuffer[0],COPY_CHUNK_SIZE);
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
		
		fd->close();
	}
	else { // receive from client, write to file
		// UNATTENDED READ: on remote connection close, close the file
		PRINTUNIFIED("Receive from client, write to file\n");
		
		std::unique_ptr<IDescriptor> fd = fdfactory.create(filepath,"wb",err);
		if (err != 0) {
			sendErrorResponse(inOutDesc);
			return;
		}
		
		sendOkResponse(inOutDesc);
		
		uint32_t readBytes,writtenBytes;
		// start streaming file from client till EOF or any error
		PRINTUNIFIED("Receiving stream...\n");
		for(;;) {
			readBytes = inOutDesc.read(&iobuffer[0],COPY_CHUNK_SIZE);
			if (readBytes <= 0) {
				PRINTUNIFIEDERROR("break, read byte count is %d\n",readBytes);
				break;
			}
			writtenBytes = fd->writeAll(&iobuffer[0],readBytes);
			if (writtenBytes < readBytes) {
				PRINTUNIFIEDERROR("break, written byte count is %d\n",writtenBytes);
				break;
			}
		}
		
		fd->close();
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
	
	struct timeval times[2] = {};
	
	uint32_t x;
	if (B0(flags)) { // access (least significant bit)
		inOutDesc.readAllOrExit(&x,sizeof(uint32_t));
		times[0].tv_sec = x;
	}
	if (B1(flags)) { // modification (second least significant bit)
		inOutDesc.readAllOrExit(&x,sizeof(uint32_t));
		times[1].tv_sec = x;
	}
	
	struct stat st = {}; // in case of expected modification of only one timestamp, take the other from here
	int ret = getFileType_(filepath,&st);
	if (ret < 0) {
		sendErrorResponse(inOutDesc);
		return;
	}
	
	// complete time structs if the user has chosen not to set both timestamps
	if (!B0(flags)) times[0].tv_sec = st.st_atime;
	if (!B1(flags)) times[1].tv_sec = st.st_mtime;
	
	ret = utimes(filepath,times);
	sendBaseResponse(ret,inOutDesc);
}

/* two flag bits that enable receiving at most both uid_t and gid_t */
void setOwnership(const char* filepath, IDescriptor& inOutDesc,uint8_t flags) {
	PRINTUNIFIED("Setting file ownership\n");
	PRINTUNIFIED("Flags: %u\n",flags);
	uint32_t owner,group;
	if (B0(flags)) inOutDesc.readAllOrExit(&owner,sizeof(uint32_t));
	if (B1(flags)) inOutDesc.readAllOrExit(&group,sizeof(uint32_t));
	
	struct stat st = {}; // in case of expected modification of only one between owner and group, take the other from here
	int ret = getFileType_(filepath,&st);
	if (ret < 0) {
		sendErrorResponse(inOutDesc);
		return;
	}
	
	if (!B0(flags)) owner = st.st_uid;
	if (!B1(flags)) group = st.st_gid;
	
	ret = chown(filepath,owner,group);
	sendBaseResponse(ret,inOutDesc);
}

/* no flag bits used, only param to receive: mode_t with permissions */
void setPermissions(const char* filepath, IDescriptor& inOutDesc) {
	PRINTUNIFIED("Setting file permissions\n");
	uint32_t perms_;
	mode_t* perms = (mode_t*)(&perms_);
	inOutDesc.readAllOrExit(&perms_,sizeof(uint32_t)); // mode_t should be 4 byte, avoiding hanging if not so
	
	int ret = chmod(filepath, *perms);
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
	
	rcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	writeStringWithLen(rcl,dirpath);
	rcl.writeAllOrExit(&mode,4);
	
	// switch additional content to propagate depending on flags
	if(B0(rqByteWithFlags.flags)) {
		if(B1(rqByteWithFlags.flags)) {
			cl.readAllOrExit(&creationStrategy,sizeof(uint8_t));
			cl.readAllOrExit(&filesize,sizeof(uint64_t));
			rcl.writeAllOrExit(&creationStrategy,sizeof(uint8_t));
			rcl.writeAllOrExit(&filesize,sizeof(uint64_t));
		}
		else {
			// nothing to propagate here
		}
	}
	else {
		// nothing to propagate here
	}
	
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
	rcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	writePairOfStringsWithPairOfLens(rcl,srcDestPaths);
	
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
	
	rcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	
	writeStringWithLen(rcl,dirpath);
	
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
		if (B0(rqByteWithFlags.flags)) {
			singleStats_resp_t sfstats = {};
			readsingleStats_resp(rcl,sfstats);
			cl.writeAllOrExit(&resp,sizeof(uint8_t));
			writesingleStats_resp(cl,sfstats);
			return;
		}
		if (B1(rqByteWithFlags.flags)) {
			folderStats_resp_t fldstats = {};
			readfolderStats_resp(rcl,fldstats);
			cl.writeAllOrExit(&resp,sizeof(uint8_t));
			writefolderStats_resp(cl,fldstats);
			return;
		}
		if (B2(rqByteWithFlags.flags)) {
			errno = 0x1234; // not yet implemented
			sendErrorResponse(cl);
			return;
		}
	}
}

// cl: local Unix socket, rcl: TLS socket
void client_hash(IDescriptor& cl, IDescriptor& rcl, request_type rqByteWithFlags) {
	uint8_t algorithm;
	cl.readAllOrExit(&algorithm, sizeof(uint8_t));
	PRINTUNIFIED("received algorithm hash position is:\t%u\n", algorithm);
	if (algorithm == 0 || algorithm >= rh_hash_maxAlgoIndex) {
		sendErrorResponse(cl);
		return;
	}
	rcl.writeAllOrExit(&rqByteWithFlags, sizeof(uint8_t));
	rcl.writeAllOrExit(&algorithm, sizeof(uint8_t));
	
	std::string filepath = readStringWithLen(cl);
	PRINTUNIFIED("received filepath to hash is:\t%s\n", filepath.c_str());
	writeStringWithLen(rcl, filepath);
	
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
void client_ls(IDescriptor& cl, IDescriptor& rcl) {
	std::string dirpath = readStringWithLen(cl);
	uint16_t dirpath_sz = dirpath.size();
	
	static constexpr uint8_t ls_rq = ACTION_LS;

	uint32_t totalRqSize = sizeof(uint8_t)+sizeof(uint16_t)+dirpath_sz;
	std::vector<uint8_t> ls_opt_rq(totalRqSize);
	uint8_t* v = &ls_opt_rq[0];
	memcpy(v,&ls_rq,sizeof(uint8_t));
	memcpy(v+sizeof(uint8_t),&dirpath_sz,sizeof(uint16_t));
	memcpy(v+sizeof(uint8_t)+sizeof(uint16_t),dirpath.c_str(),dirpath_sz);
	rcl.writeAllOrExit(v,totalRqSize);
	
	// read and pass-through response
	
	uint8_t resp;
	int receivedErrno;
	rcl.readAllOrExit(&resp,sizeof(uint8_t));
	if (resp != 0) { // list dir error
		rcl.readAllOrExit(&receivedErrno,sizeof(int));
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
		cl.writeAllOrExit(&receivedErrno,sizeof(int));
		return;
	}
	else { // OK
		cl.writeAllOrExit(&resp,sizeof(uint8_t));
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

// FIXME mainly common code between client_upload and server_download, refactor with if statements (conditionally toggle different code blocks)
// client sending upload request and content (and sending progress indication back on cl unix socket)
// client UPLOADS to server
void client_upload(IDescriptor& cl, IDescriptor& rcl) {
	static constexpr uint8_t up_rq = ACTION_UPLOAD;
	// receive list of source-destination path pairs from cl
	std::vector<std::pair<std::string,std::string>> v = receivePathPairsList(cl);
	
	// count all files in the selection
	std::unordered_map<std::string,sts_sz> descendantCountMap;

	sts_sz counts = {}; // no need to put this in the map

	for (auto& item : v) {
		sts_sz itemTotals = countTotalStatsIntoMap(item.first,descendantCountMap);
		counts.tFiles += itemTotals.tFiles;
		counts.tFolders += itemTotals.tFolders;
        counts.tSize += itemTotals.tSize;
	}
	
	// send counts.tFiles on local descriptor
	cl.writeAllOrExit(&(counts.tFiles),sizeof(uint64_t));
    // send total size as well
    cl.writeAllOrExit(&(counts.tSize),sizeof(uint64_t));
	
	// send "client upload" request to server
	rcl.writeAllOrExit(&up_rq, sizeof(uint8_t));
	
	for (auto& item : v)
		genericUploadBasicRecursiveImplWithProgress(item.first,item.second,rcl,&cl);
	
	// send end of files indicator to local descriptor
	cl.writeAllOrExit(&maxuint_2,sizeof(uint64_t));
	
	// send end of list to remote descriptor
	static constexpr uint8_t endOfList = 0xFF;
	rcl.writeAllOrExit(&endOfList,sizeof(uint8_t));
}

// client DOWNLOADS from server
void client_download(IDescriptor& cl, IDescriptor& rcl) {
	static constexpr uint8_t down_rq = ACTION_DOWNLOAD;
	// receive list of source-destination path pairs from cl
	std::vector<std::pair<std::string,std::string>> v = receivePathPairsList(cl);
	
	// send "client download" request to server
	rcl.writeAllOrExit(&down_rq, sizeof(uint8_t));
	
	// send list of path pairs (files and maybe-not-empty folders)
	// receive back items with type flag, file full path, size and content, till list end
	sendPathPairsList(v,rcl);
	
	// receive total number of files to be received
	uint64_t totalFiles,totalSize;
	rcl.readAllOrExit(&totalFiles,sizeof(uint64_t));
	// NEW receive total size as well
	rcl.readAllOrExit(&totalSize,sizeof(uint64_t));
	
	
	// propagate total files to local socket
	cl.writeAllOrExit(&totalFiles,sizeof(uint64_t));
	// NEW propagate total size as well
	cl.writeAllOrExit(&totalSize,sizeof(uint64_t));
	
	downloadRemoteItems(rcl,&cl);
}

// ----------------@@@@@@@@@@@@@@@@@ rhss related logic and variable moved into xre.h

// avoids forking, just for debug purposes
void plainP7ZipSession(IDescriptor& cl, request_type rq) {
    switch (rq.request) {
        case ACTION_COMPRESS:
            compressToArchive(cl,rq.flags);
            break;
        case ACTION_EXTRACT:
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
	switch (rq.request) {
		case ACTION_COMPRESS:
			compressToArchive(cl, rq.flags);
			break;
		case ACTION_EXTRACT:
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

constexpr struct linger lo = {1, 0};

void forkServerAcceptor(int cl) {
	// receive byte indicating whether to serve entire filesystem (0x00) or only custom directory (non-zero byte)
	uint8_t x;
	PosixDescriptor pd_cl(cl);
	pd_cl.readAllOrExit(&x,sizeof(uint8_t));
	
	if(x) {
		std::string filepath = readStringWithLen(pd_cl);
		PRINTUNIFIED("received directory to be offered is:\t%s\n", filepath.c_str());
		
		rhss_currentlyServedDirectory = filepath;
	}
	
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
		rhss = socket(AF_INET, SOCK_STREAM, 0);
		if(rhss == -1) {
		    PRINTUNIFIEDERROR("Unable to create TLS server socket\n");
		    sendErrorResponse(pd_cl);
			exit(-1);
		}
		
		struct sockaddr_in socket_info = {};
		socket_info.sin_family = AF_INET;
		socket_info.sin_port = htons(rhServerPort);
		
		socket_info.sin_addr.s_addr = INADDR_ANY;

		if(bind(rhss, reinterpret_cast<struct sockaddr*>(&socket_info), sizeof(struct sockaddr)) != 0)
		{
		    close(rhss);
		    PRINTUNIFIEDERROR("TLS server bind failed\n");
		    sendErrorResponse(pd_cl);
			exit(-1);
		}

		// up to 10 concurrent clients (both with 2 sessions) allowed
		if(listen(rhss, MAX_CLIENTS) != 0)
		{
		    close(rhss);
		    PRINTUNIFIEDERROR("TLS server listen failed\n");
		    sendErrorResponse(pd_cl);
			exit(-1);
		}
		
		PRINTUNIFIED("remote rhServer acceptor process started, pid: %d\n",getpid());
		sendOkResponse(pd_cl);
		
		// from now on, server session threads communicate with local client over rhss_local
		rhss_local = cl;
		cl = -1;
		
		for(;;) {
			struct sockaddr_in st{};
			socklen_t stlen{};
			int remoteCl = accept(rhss,(struct sockaddr *)&st,&stlen); // (#1) peer info retrieved and converted to string in spawned thread
			if (remoteCl == -1) {
				PRINTUNIFIEDERROR("accept error on remote server\n");
				continue;
			}
			
			// this is needed in order for (client) write operations to fail when the remote (server) socket is closed
			// (e.g. client is performing unacked writing and server disconnects)
			// so that client does not hangs when server abruptly closes connections
			setsockopt(remoteCl, SOL_SOCKET, SO_LINGER, &lo, sizeof(lo));
			
			std::thread serverToClientThread(tlsServerSession,remoteCl);
			serverToClientThread.detach();
		}
		
		}
		catch (threadExitThrowable& i) {
			PRINTUNIFIEDERROR("forkXRE_child...\n");
		}
	}
	else { // in parent
		rhss_currentlyServedDirectory.clear(); // operation methods are in common, clear in parent otherwise we will have restrictions on local operations
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

// defaults to 5 seconds timeout
int connect_with_timeout(int& sock_fd, struct addrinfo* p, unsigned timeout_seconds = 5) {
	int res;
	//~ struct sockaddr_in addr; 
	long arg;
	fd_set myset; 
	struct timeval tv; 
	int valopt; 
	socklen_t lon; 

	// Create socket 
	// sock_fd = socket(AF_INET, SOCK_STREAM, 0);
	sock_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
	
	if (sock_fd < 0) {
		PRINTUNIFIEDERROR("Error creating socket (%d %s)\n", errno, strerror(errno)); 
		return -1; 
	}

	// Set non-blocking 
	if( (arg = fcntl(sock_fd, F_GETFL, nullptr)) < 0) {
		PRINTUNIFIEDERROR("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
		return -2;
	}
	arg |= O_NONBLOCK; 
	if( fcntl(sock_fd, F_SETFL, arg) < 0) {
		PRINTUNIFIEDERROR("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
		return -3;
	}
	// Trying to connect with timeout 
	// res = connect(sock_fd, (struct sockaddr *)&addr, sizeof(addr)); 
	res = connect(sock_fd, p->ai_addr, p->ai_addrlen);
	if (res < 0) {
		if (errno == EINPROGRESS) {
			PRINTUNIFIEDERROR("EINPROGRESS in connect() - selecting\n"); 
			for(;;) {
				tv.tv_sec = timeout_seconds; 
				tv.tv_usec = 0; 
				FD_ZERO(&myset); 
				FD_SET(sock_fd, &myset);
				res = select(sock_fd+1, nullptr, &myset, nullptr, &tv); 
				if (res < 0 && errno != EINTR) {
					PRINTUNIFIEDERROR("Error connecting %d - %s\n", errno, strerror(errno)); 
					return -4;
				}
				else if (res > 0) {
					// Socket selected for write 
					lon = sizeof(int); 
					if (getsockopt(sock_fd, SOL_SOCKET, SO_ERROR, (void*)(&valopt), &lon) < 0) {
						PRINTUNIFIEDERROR("Error in getsockopt() %d - %s\n", errno, strerror(errno)); 
						return -5; 
					}
					// Check the value returned... 
					if (valopt) {
						PRINTUNIFIEDERROR("Error in delayed connection() %d - %s\n", valopt, strerror(valopt)); 
						return -6;
					}
					break; 
				}
				else {
					PRINTUNIFIEDERROR("Timeout in select() - Cancelling!\n"); 
					return -7;
				}
			}
		}
		else {
		PRINTUNIFIEDERROR("Error connecting %d - %s\n", errno, strerror(errno)); 
		return -8;
		}
	}
	// Set to blocking mode again... 
	if( (arg = fcntl(sock_fd, F_GETFL, nullptr)) < 0) {
		PRINTUNIFIEDERROR("Error fcntl(..., F_GETFL) (%s)\n", strerror(errno)); 
		return -9;
	}
	arg &= (~O_NONBLOCK); 
	if( fcntl(sock_fd, F_SETFL, arg) < 0) {
		PRINTUNIFIEDERROR("Error fcntl(..., F_SETFL) (%s)\n", strerror(errno)); 
		return -10;
	}
	return 0; // ok, at this point the socket is connected and again in blocking mode
}

void tlsClientSessionEventLoop(RingBuffer& inRb, Botan::TLS::Client& client, IDescriptor& cl) {
  TLSDescriptor rcl(inRb,client);
  try { 
	PRINTUNIFIED("In TLS client event loop...\n");
	for(;;) {
		// read request from cl, propagate to rcl
		request_type rq = {};
		cl.readAllOrExit(&rq,sizeof(rq));
		switch (rq.request) {
			case ACTION_LS:
				client_ls(cl,rcl);
				break;
			case ACTION_CREATE:
				client_createFileOrDirectory(cl,rcl,rq);
				break;
			case ACTION_LINK:
				client_createHardOrSoftLink(cl,rcl,rq);
				break;
			case ACTION_STATS:
				client_stats(cl,rcl,rq);
				break;
			case ACTION_HASH:
				client_hash(cl,rcl,rq);
				break;
			case ACTION_DOWNLOAD:
				client_download(cl,rcl);
				break;
			case ACTION_UPLOAD:
				client_upload(cl,rcl);
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
	
    int remoteCl = -1;
	struct addrinfo hints, *servinfo, *p;
	int rv;

	PRINTUNIFIED("Populating hints...\n");
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // use AF_INET to force IPv4, AF_INET6 to force IPv6, AF_UNSPEC to allow both
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_protocol = IPPROTO_TCP;
	
	std::string port_s = std::to_string(port);
	
	PRINTUNIFIED("Invoking getaddrinfo...\n");
	if ((rv = getaddrinfo(target.c_str(), port_s.c_str(), &hints, &servinfo)) != 0) {
		PRINTUNIFIEDERROR("getaddrinfo error: %s\n", gai_strerror(rv));
		return;
	}
	
	PRINTUNIFIED("Looping through getaddrinfo results...\n");
	// loop through all the results and connect to the first we can
	for(p = servinfo; p != nullptr; p = p->ai_next) {
		PRINTUNIFIED("getaddrinfo item\n");
		
		// NEW, with timeout
		rv = connect_with_timeout(remoteCl, p);
		if (rv == 0) break;
		else {
			PRINTUNIFIEDERROR("Timeout or connection error %d\n",rv);
			close(remoteCl);
		}
	}
	PRINTUNIFIED("getaddrinfo end results\n");
	
	if (p == nullptr) {
		freeaddrinfo(servinfo);
		PRINTUNIFIED("Could not create socket or connect\n");
		errno = 0x323232;
		sendErrorResponse(cl);
		return;
	}
	PRINTUNIFIED("freeaddrinfo...\n");
	freeaddrinfo(servinfo);
	PRINTUNIFIED("Remote client session connected to server %s, port %d\n",target.c_str(),port);
	sendOkResponse(cl); // OK, from now on java client can communicate with remote server using this local socket
	
	PosixDescriptor pd_remoteCl(remoteCl);
	TLS_Client tlsClient(tlsClientSessionEventLoop,cl,pd_remoteCl);
	tlsClient.go();
	
	// at the end, close the sockets
	pd_remoteCl.close();
	cl.close();
}

// generate SSH keys in PKCS8 format via Botan
void ssh_keygen(IDescriptor& inOutDesc, uint8_t flags) {
	// TODO for now, only RSA supported, ignore the flag bits of rq and receive key size
	uint32_t keySize;
	inOutDesc.readAllOrExit(&keySize,sizeof(uint32_t));
	PRINTUNIFIED("Received key size: %u\n", keySize);
	PRINTUNIFIED("Generating key pair...");
	Botan::AutoSeeded_RNG rng;
	Botan::RSA_PrivateKey prv(rng,keySize);
	PRINTUNIFIED("Generation complete, encoding to PEM...");
	std::string prv_s = Botan::PKCS8::PEM_encode(prv);
	uint32_t prv_s_len = prv_s.size();
    std::string pub_s = Botan::X509::PEM_encode(prv);
    uint32_t pub_s_len = pub_s.size();
    PRINTUNIFIED("Encoding complete");
    sendOkResponse(inOutDesc); // actually not needed
    
    inOutDesc.writeAllOrExit(&prv_s_len,sizeof(uint32_t));
    inOutDesc.writeAllOrExit(prv_s.c_str(),prv_s_len);
    inOutDesc.writeAllOrExit(&pub_s_len,sizeof(uint32_t));
    inOutDesc.writeAllOrExit(pub_s.c_str(),pub_s_len);
}

// request types to be served in a new thread
void serveRequest(int intcl, request_type rq) {
	PosixDescriptor cl(intcl);
	
	switch (rq.request) {
		case ACTION_COMPRESS:
		case ACTION_EXTRACT:
			forkP7ZipSession(cl,rq);
			break;
		case ACTION_LS:
			listDirOrArchive(cl, rq.flags);
			break;
		case ACTION_COPY:
			copyFileOrDirectoryFullNew(cl);
			break;
		case ACTION_MOVE:
			moveFileOrDirectory(cl, rq.flags);
			break;
		case ACTION_DELETE:
			deleteFile(cl);
			break;
		case ACTION_STATS:
			stats(cl, rq.flags);
			break;
		case ACTION_EXISTS:
			existsIsFileIsDir(cl, rq.flags);
			break;
		case ACTION_CREATE:
			createFileOrDirectory(cl, rq.flags);
			break;
		case ACTION_HASH:
			hashFile(cl);
			break;
		case ACTION_FIND:
			try {
				findNamesAndContent(cl, rq.flags);
			}
			catch(...) {
				on_find_thread_exit_function();
				throw; // rethrow in order to exit from the for loop of 
			}
			break;
		case ACTION_KILL:
			killProcess(cl);
			break;
		case ACTION_GETPID:
			getThisPid(cl);
			break;
		//~ case ACTION_FORK:
			//~ forkNewRH(intcl,unixSocketFd);
			//~ break;
		case ACTION_FILE_IO:
			readOrWriteFile(cl,rq.flags);
			break;
		//~ case ACTION_CANCEL:
			//~ cancelRunningOperations(intcl);
			//~ break;
			
		case ACTION_SETATTRIBUTES:
			setAttributes(cl);
			break;
			
		case ACTION_SSH_KEYGEN:
			ssh_keygen(cl,rq.flags);
			break;
		
		case ACTION_LINK:
			createHardOrSoftLink(cl,rq.flags);
			break;
		
		case REMOTE_SERVER_MANAGEMENT:
			switch(rq.flags) {
				case 0: // stop if active
					killServerAcceptor(cl);
					break;
				case 7: // 111 start
					forkServerAcceptor(intcl);
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
		
		case REMOTE_CONNECT:
			// connect to rh remote server
			tlsClientSession(cl);
			break;
		// client disconnect: on process exit, or on Java client disconnect from local unix socket
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
      request_type rq = {};
      pd_cl.readAllOrExit(&rq, sizeof(rq));
      
      PRINTUNIFIED("request 5-bits received:\t%u\n", rq.request);
      PRINTUNIFIED("request flags (3-bits) received:\t%u\n", rq.flags);
      
      if (rq.request == ACTION_EXIT) {
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
	// FIXME return value not working for lib.Load
	if (!lib.Load(NDLL::GetModuleDirPrefix() + FTEXT(kDllName))) {
		PrintError("p7zip - Can not load 7-zip library");
		_Exit(71);
	}
  
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

void xreMain() {
    rhss = getServerSocket();
    printNetworkInfo();
    acceptLoop(rhss);
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
	registerExitRoutines();
	print_roothelper_version();
	if (prog_is_xre(argv[0])) {
		if(argc >= 2 && mode_is_help(argv[1])) {
			print_help(argv[0]);
		}
		else {
			PRINTUNIFIED("xre mode by filename, won't accept further command line arguments\n");
			xreMain();
		}
	}
	else if (argc >= 2) {
		if (mode_is_xre(argv[1])) {
			PRINTUNIFIED("xre mode, won't accept further command line arguments\n");
			xreMain();
		}
		else if(mode_is_help(argv[1])) {
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
