#ifndef _WIN32
#ifndef __RH_ARCHIVE_UTILS_H__
#define __RH_ARCHIVE_UTILS_H__
#include <cstdint>
#include <utility>

#include "StdAfx.h"

#include "../CPP/Common/MyWindows.h"

#include "../CPP/Common/Defs.h"
#include "../CPP/Common/MyInitGuid.h"

#include "../CPP/Common/IntToString.h"
#include "../CPP/Common/StringConvert.h"

#include "../CPP/Windows/TimeUtils.h"
#include "../CPP/Windows/DLL.h"
#include "../CPP/Windows/FileDir.h"
#include "../CPP/Windows/FileFind.h"
#include "../CPP/Windows/FileName.h"
#include "../CPP/Windows/NtCheck.h"
#include "../CPP/Windows/PropVariant.h"
#include "../CPP/Windows/PropVariantConv.h"

#include "../CPP/7zip/Common/FileStreams.h"

#include "../CPP/7zip/Archive/IArchive.h"

#include "../CPP/7zip/IPassword.h"
#include "../C/7zVersion.h"

#include "archiveTypeDetector.h"


#include "af_unix_utils.h"
#include "desc/PosixDescriptor.h"

// You can find the list of all GUIDs in Guid.txt file.
// use another CLSIDs, if you want to support other formats (zip, rar, ...).
// {23170F69-40C1-278A-1000-000110070000}

DEFINE_GUID(CLSID_CFormat7z,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x07, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatXz,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x0C, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatRar,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x03, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatRar5,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0xCC, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatZip,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x01, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatCab,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x08, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatGz,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0xEF, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatBz2,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0x02, 0x00, 0x00);
DEFINE_GUID(CLSID_CFormatTar,
            0x23170F69, 0x40C1, 0x278A, 0x10, 0x00, 0x00, 0x01, 0x10, 0xEE, 0x00, 0x00);

// header-based archive type detection
// elements must have same ordering of enum in archiveTypeDetector.h
std::vector<GUID> archiveGUIDs = {
	CLSID_CFormat7z,
	CLSID_CFormatXz,
	CLSID_CFormatRar,
	CLSID_CFormatRar5,
	CLSID_CFormatZip,
	CLSID_CFormatCab,
	CLSID_CFormatGz,
	CLSID_CFormatBz2,
	CLSID_CFormatTar
};

using namespace NWindows;
using namespace NFile;
using namespace NDir;

// #define kDllName "7z.dll"
#define kDllName "lib7z.dll" // string will be replaced with lib7z.so before library loading 

static NDLL::CLibrary lib;

static AString FStringToConsoleString(const FString &s) {
  return GetOemString(fs2us(s));
}

static FString CmdStringToFString(const char *s) {
  return us2fs(GetUnicodeString(s));
}

static void PrintString(const UString &s) {
  PRINTUNIFIED("%s", (LPCSTR)GetOemString(s));
}

static void PrintString(const AString &s) {
  PRINTUNIFIED("%s", (LPCSTR)s);
}

static void PrintNewLine() {
  PrintString("\n");
}

static void PrintError(const char *message, const FString &name) {
  PRINTUNIFIED("Error: %s", (LPCSTR)message);
  PrintNewLine();
  PrintString(FStringToConsoleString(name));
  PrintNewLine();
}

static void PrintError(const AString &s) {
  PrintNewLine();
  PrintString(s);
  PrintNewLine();
}

static HRESULT IsArchiveItemProp(IInArchive *archive, UInt32 index, PROPID propID, bool &result)
{
  NCOM::CPropVariant prop;
  RINOK(archive->GetProperty(index, propID, &prop));
  if (prop.vt == VT_BOOL)
    result = VARIANT_BOOLToBool(prop.boolVal);
  else if (prop.vt == VT_EMPTY)
    result = false;
  else
    return E_FAIL;
  return S_OK;
}

static HRESULT IsArchiveItemFolder(IInArchive *archive, UInt32 index, bool &result)
{
  return IsArchiveItemProp(archive, index, kpidIsDir, result);
}

static const wchar_t *kEmptyFileAlias = L"[Content]";

//////////////////////////////////////////////////////////////
// Archive Open callback class

class CArchiveOpenCallback : public IArchiveOpenCallback,
                             public ICryptoGetTextPassword,
                             public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

  STDMETHOD(SetTotal)
  (const UInt64 *files, const UInt64 *bytes) override;
  STDMETHOD(SetCompleted)
  (const UInt64 *files, const UInt64 *bytes) override;

  STDMETHOD(CryptoGetTextPassword)
  (BSTR *password) override;

  bool PasswordIsDefined;
  UString Password;

  CArchiveOpenCallback() : PasswordIsDefined(false) {}
};

STDMETHODIMP CArchiveOpenCallback::SetTotal(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
  return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::SetCompleted(const UInt64 * /* files */, const UInt64 * /* bytes */)
{
  return S_OK;
}

STDMETHODIMP CArchiveOpenCallback::CryptoGetTextPassword(BSTR *password)
{
  if (!PasswordIsDefined)
  {
    // You can ask real password here from user
    // Password = GetPassword(OutStream);
    // PasswordIsDefined = true;
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}

//////////////////////////////////////////////////////////////
// Archive Extracting callback class

static const char *kTestingString = "Testing     ";
static const char *kExtractingString = "Extracting  ";
static const char *kSkippingString = "Skipping    ";

static const char *kUnsupportedMethod = "Unsupported Method";
static const char *kCRCFailed = "CRC Failed";
static const char *kDataError = "Data Error";
static const char *kUnavailableData = "Unavailable data";
static const char *kUnexpectedEnd = "Unexpected end of data";
static const char *kDataAfterEnd = "There are some data after the end of the payload data";
static const char *kIsNotArc = "Is not archive";
static const char *kHeadersError = "Headers Error";

class CArchiveExtractCallback : public IArchiveExtractCallback,
                                public ICryptoGetTextPassword,
                                public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP1(ICryptoGetTextPassword)

  // IProgress
  STDMETHOD(SetTotal)
  (UInt64 size) override;
  STDMETHOD(SetCompleted)
  (const UInt64 *completeValue) override;

  // IArchiveExtractCallback
  STDMETHOD(GetStream)
  (UInt32 index, ISequentialOutStream **outStream, Int32 askExtractMode) override;
  STDMETHOD(PrepareOperation)
  (Int32 askExtractMode) override;
  STDMETHOD(SetOperationResult)
  (Int32 resultEOperationResult) override;

  // ICryptoGetTextPassword
  STDMETHOD(CryptoGetTextPassword)
  (BSTR *aPassword) override;

private:
  CMyComPtr<IInArchive> _archiveHandler;
  FString _directoryPath; // Output directory
  UString _filePath;      // name inside arcvhive
  FString _diskFilePath;  // full path to file on disk
  bool _extractMode;
  struct CProcessedFileInfo
  {
    FILETIME MTime;
    UInt32 Attrib;
    bool isDir;
    bool AttribDefined;
    bool MTimeDefined;
  } _processedFileInfo;

  COutFileStream *_outFileStreamSpec;
  CMyComPtr<ISequentialOutStream> _outFileStream;

  CObjectVector<NWindows::NFile::NDir::CDelayedSymLink> _delayedSymLinks;

public:
  void Init(IInArchive *archiveHandler, const FString &directoryPath);
  
  /** BEGIN RootHelper */
  // - Progress fields - //
  // descriptor for publishing progress
  IDescriptor* inOutDesc;
  void setDesc(IDescriptor* inOut_) {
	  inOutDesc = inOut_;
  }
  uint64_t lastProgress,totalSize;
  // - Common subdir-to-archive path length for relative extract path truncate (in wide char)
  uint32_t subDirLengthForPathTruncateInWideChars = 0;
  void setSubDirLengthForPathTruncate(uint32_t l) {
      subDirLengthForPathTruncateInWideChars = l;
  }
  
  std::wstring streamArchiveOnlyChildFilenameOnly; // populated only in XZ,GZ,BZ2
  /** END RootHelper */


  HRESULT SetFinalAttribs();

  UInt64 NumErrors;
  bool PasswordIsDefined;
  UString Password;

  CArchiveExtractCallback() : PasswordIsDefined(false) {}
};

void CArchiveExtractCallback::Init(IInArchive *archiveHandler, const FString &directoryPath)
{
  NumErrors = 0;
  _archiveHandler = archiveHandler;
  _directoryPath = directoryPath;
  NName::NormalizeDirPathPrefix(_directoryPath);
}

STDMETHODIMP CArchiveExtractCallback::SetTotal(UInt64 size)
{
  totalSize = size;
  PRINTUNIFIED("setTotal called in extract, size is %llu\n",size);
  writeAllOrExitProcess(*inOutDesc, &size, sizeof(uint64_t)); // publish total information
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::SetCompleted(const UInt64* completeValue)
{
  if (*completeValue == totalSize || *completeValue - lastProgress >= 1000000) { // do not waste too much cpu in publishing progress
	PRINTUNIFIED("setCompleted called in extract, current value is %llu\n",*completeValue);
	writeAllOrExitProcess(*inOutDesc, completeValue, sizeof(uint64_t)); // publish progress information
	lastProgress = *completeValue;
  }
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::GetStream(UInt32 index,
                                                ISequentialOutStream **outStream, Int32 askExtractMode)
{
  *outStream = nullptr;
  _outFileStream.Release();

  {
    // Get Name
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidPath, &prop));

    UString fullPath;
    if (prop.vt == VT_EMPTY) {
		if(streamArchiveOnlyChildFilenameOnly.empty())
			fullPath = kEmptyFileAlias;
		else fullPath = streamArchiveOnlyChildFilenameOnly.c_str();
	}
    else
    {
      if (prop.vt != VT_BSTR)
        return E_FAIL;
      fullPath = prop.bstrVal;
    }

    if (subDirLengthForPathTruncateInWideChars == 0)
        _filePath = fullPath;
    else { // strip common subdir prefix
        if (fullPath.Len()<subDirLengthForPathTruncateInWideChars) {
            PRINTUNIFIED("illegal length, hopefully on skipped entry, using full path\n");
            _filePath = fullPath;
        }
        else _filePath = UString(fullPath.Ptr(subDirLengthForPathTruncateInWideChars));
    }
  }

  if (askExtractMode != NArchive::NExtract::NAskMode::kExtract)
    return S_OK;

  {
    // Get Attrib
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidAttrib, &prop));
    if (prop.vt == VT_EMPTY)
    {
      _processedFileInfo.Attrib = 0;
      _processedFileInfo.AttribDefined = false;
    }
    else
    {
      if (prop.vt != VT_UI4)
        return E_FAIL;
      _processedFileInfo.Attrib = prop.ulVal;
      _processedFileInfo.AttribDefined = true;
    }
  }

  RINOK(IsArchiveItemFolder(_archiveHandler, index, _processedFileInfo.isDir));

  {
    // Get Modified Time
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidMTime, &prop));
    _processedFileInfo.MTimeDefined = false;
    switch (prop.vt)
    {
    case VT_EMPTY:
      // _processedFileInfo.MTime = _utcMTimeDefault;
      break;
    case VT_FILETIME:
      _processedFileInfo.MTime = prop.filetime;
      _processedFileInfo.MTimeDefined = true;
      break;
    default:
      return E_FAIL;
    }
  }
  {
    // Get Size
    NCOM::CPropVariant prop;
    RINOK(_archiveHandler->GetProperty(index, kpidSize, &prop));
    UInt64 newFileSize;
    /* bool newFileSizeDefined = */ ConvertPropVariantToUInt64(prop, newFileSize);
  }

  {
    // Create folders for file
    int slashPos = _filePath.ReverseFind_PathSepar();
    // int slashPos = _utfFilePath.ReverseFind_PathSepar();
    if (slashPos >= 0) {
      CreateComplexDir(_directoryPath + us2fs(_filePath.Left(slashPos)));
      // CreateComplexDir(_directoryPath + _utfFilePath.Left(slashPos));
    }
  }

  FString fullProcessedPath = _directoryPath + us2fs(_filePath);
  // FString fullProcessedPath = _directoryPath + _utfFilePath;
  
  _diskFilePath = fullProcessedPath;

  if (_processedFileInfo.isDir)
  {
    CreateComplexDir(fullProcessedPath); // should be equivalent to mkpath or mkpathCopyPermissionsFromNearestAncestor
  }
  else
  {
    NFind::CFileInfo fi;
    if (fi.Find(fullProcessedPath))
    {
      if (!DeleteFileAlways(fullProcessedPath))
      {
        PrintError("Can not delete output file", fullProcessedPath);
        return E_ABORT;
      }
    }

    _outFileStreamSpec = new COutFileStream;
    CMyComPtr<ISequentialOutStream> outStreamLoc(_outFileStreamSpec);
    if (!_outFileStreamSpec->Open(fullProcessedPath, CREATE_ALWAYS))
    {
      PrintError("Can not open output file", fullProcessedPath);
      return E_ABORT;
    }
    _outFileStream = outStreamLoc;
    *outStream = outStreamLoc.Detach();
  }
  return S_OK;
}

STDMETHODIMP CArchiveExtractCallback::PrepareOperation(Int32 askExtractMode)
{
  _extractMode = false;
  switch (askExtractMode)
  {
  case NArchive::NExtract::NAskMode::kExtract:
    _extractMode = true;
    break;
  }

  switch (askExtractMode)
  {
  case NArchive::NExtract::NAskMode::kExtract:
    PrintString(kExtractingString);
    break;
  case NArchive::NExtract::NAskMode::kTest:
    PrintString(kTestingString);
    break;
  case NArchive::NExtract::NAskMode::kSkip:
    PrintString(kSkippingString);
    break;
  }

  PrintString(_filePath);
  return S_OK;
}

Int32 latestExtractResult;

STDMETHODIMP CArchiveExtractCallback::SetOperationResult(Int32 operationResult)
{
  // to be returned if not OK from extractFromArchive (at least to indicate wrong password provided)
  latestExtractResult = operationResult;
  
  switch (operationResult)
  {
  case NArchive::NExtract::NOperationResult::kOK:
    break;
  default:
  {
    NumErrors++;
    PrintString("  :  ");
    const char *s = nullptr;
    switch (operationResult)
    {
    case NArchive::NExtract::NOperationResult::kUnsupportedMethod:
      s = kUnsupportedMethod;
      break;
    case NArchive::NExtract::NOperationResult::kCRCError:
      s = kCRCFailed;
      break;
    case NArchive::NExtract::NOperationResult::kDataError:
      s = kDataError;
      break;
    case NArchive::NExtract::NOperationResult::kUnavailable:
      s = kUnavailableData;
      break;
    case NArchive::NExtract::NOperationResult::kUnexpectedEnd:
      s = kUnexpectedEnd;
      break;
    case NArchive::NExtract::NOperationResult::kDataAfterEnd:
      s = kDataAfterEnd;
      break;
    case NArchive::NExtract::NOperationResult::kIsNotArc:
      s = kIsNotArc;
      break;
    case NArchive::NExtract::NOperationResult::kHeadersError:
      s = kHeadersError;
      break;
    }
    if (s)
    {
      PrintString("Error : ");
      PrintString(s);
    }
    else
    {
      char temp[16];
      ConvertUInt32ToString(operationResult, temp);
      PrintString("Error #");
      PrintString(temp);
    }
  }
  }

  if (_outFileStream)
  {
    if (_processedFileInfo.MTimeDefined)
      _outFileStreamSpec->SetMTime(&_processedFileInfo.MTime);
    RINOK(_outFileStreamSpec->Close());
  }
  _outFileStream.Release();
  if (_extractMode && _processedFileInfo.AttribDefined)
    SetFileAttrib(_diskFilePath, _processedFileInfo.Attrib, &_delayedSymLinks);
  PrintNewLine();
  return S_OK;
}

HRESULT CArchiveExtractCallback::SetFinalAttribs()
{
  HRESULT result = S_OK;

  for (int i = 0; i != _delayedSymLinks.Size(); ++i)
    if (!_delayedSymLinks[i].Create())
      result = E_FAIL;

  _delayedSymLinks.Clear();

  return result;
}

STDMETHODIMP CArchiveExtractCallback::CryptoGetTextPassword(BSTR *password)
{
  if (!PasswordIsDefined)
  {
    // You can ask real password here from user
    // Password = GetPassword(OutStream);
    // PasswordIsDefined = true;
    PrintError("Password is not defined");
    return E_ABORT;
  }
  return StringToBstr(Password, password);
}

//////////////////////////////////////////////////////////////
// Archive Creating callback class

struct CDirItem
{
  UInt64 Size;
  FILETIME CTime;
  FILETIME ATime;
  FILETIME MTime;
  UString Name;
  FString FullPath;
  UInt32 Attrib;

  bool isDir() const { return (Attrib & FILE_ATTRIBUTE_DIRECTORY) != 0; }
};

class CArchiveUpdateCallback : public IArchiveUpdateCallback2,
                               public ICryptoGetTextPassword2,
                               public CMyUnknownImp
{
public:
  MY_UNKNOWN_IMP2(IArchiveUpdateCallback2, ICryptoGetTextPassword2)

  // IProgress
  STDMETHOD(SetTotal)
  (UInt64 size) override;
  STDMETHOD(SetCompleted)
  (const UInt64 *completeValue) override;

  // IUpdateCallback2
  STDMETHOD(GetUpdateItemInfo)
  (UInt32 index,
   Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive) override;
  STDMETHOD(GetProperty)
  (UInt32 index, PROPID propID, PROPVARIANT *value) override;
  STDMETHOD(GetStream)
  (UInt32 index, ISequentialInStream **inStream) override;
  STDMETHOD(SetOperationResult)
  (Int32 operationResult) override;
  STDMETHOD(GetVolumeSize)
  (UInt32 index, UInt64 *size) override;
  STDMETHOD(GetVolumeStream)
  (UInt32 index, ISequentialOutStream **volumeStream) override;

  STDMETHOD(CryptoGetTextPassword2)
  (Int32 *passwordIsDefined, BSTR *password) override ;

public:
  /** BEGIN RootHelper - Progress fields */
  // file descriptor for publishing progress
  IDescriptor* inOutDesc;
  void setDesc(IDescriptor* inOut_) {
	  inOutDesc = inOut_;
  }
  uint64_t lastProgress,totalSize;
  /** END RootHelper - Progress fields */

  CRecordVector<UInt64> VolumesSizes;
  UString VolName;
  UString VolExt;

  FString DirPrefix;
  const CObjectVector<CDirItem> *DirItems;

  bool PasswordIsDefined;
  UString Password;
  bool AskPassword;

  bool m_NeedBeClosed;

  FStringVector FailedFiles;
  CRecordVector<HRESULT> FailedCodes;

  CArchiveUpdateCallback() : PasswordIsDefined(false), AskPassword(false), DirItems(nullptr){};

  ~CArchiveUpdateCallback() override { Finilize(); }
  HRESULT Finilize();

  void Init(const CObjectVector<CDirItem> *dirItems)
  {
    DirItems = dirItems;
    m_NeedBeClosed = false;
    FailedFiles.Clear();
    FailedCodes.Clear();
  }
};

STDMETHODIMP CArchiveUpdateCallback::SetTotal(UInt64 size)
{
  totalSize = size;
  PRINTUNIFIED("setTotal called in update, size is %llu\n",size);
  writeAllOrExitProcess(*inOutDesc, &size, sizeof(uint64_t)); // publish total information
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetCompleted(const UInt64* completeValue)
{
    if (*completeValue == totalSize || *completeValue - lastProgress >= 1000000) { // do not waste too much cpu in publishing progress
        PRINTUNIFIED("setCompleted called in update, current value is %llu\n",*completeValue);
        writeAllOrExitProcess(*inOutDesc, completeValue, sizeof(uint64_t)); // publish progress information
        lastProgress = *completeValue;
    }
    return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetUpdateItemInfo(UInt32 /* index */,
                                                       Int32 *newData, Int32 *newProperties, UInt32 *indexInArchive)
{
  if (newData)
    *newData = BoolToInt(true);
  if (newProperties)
    *newProperties = BoolToInt(true);
  if (indexInArchive)
    *indexInArchive = (UInt32)(Int32)-1;
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
  // PRINTUNIFIED("GetProperty invoked for index %u",index);
  NCOM::CPropVariant prop;

  if (propID == kpidIsAnti)
  {
    prop = false;
    prop.Detach(value);
    return S_OK;
  }

  {
    const CDirItem &dirItem = (*DirItems)[index];
    switch (propID)
    {
    case kpidPath:
      prop = dirItem.Name;
      break;
    case kpidIsDir:
      prop = dirItem.isDir();
      break;
    case kpidSize:
      prop = dirItem.Size;
      break;
    case kpidAttrib:
      prop = dirItem.Attrib;
      break;
    case kpidCTime:
      prop = dirItem.CTime;
      break;
    case kpidATime:
      prop = dirItem.ATime;
      break;
    case kpidMTime:
      prop = dirItem.MTime;
      break;
    }
  }
  prop.Detach(value);
  return S_OK;
}

HRESULT CArchiveUpdateCallback::Finilize()
{
    if (m_NeedBeClosed)
    {
        PrintNewLine();
        m_NeedBeClosed = false;
    }
    return S_OK;
}

static void GetStream2(const wchar_t *name)
{
  PrintString("Compressing  ");
  if (name[0] == 0)
    name = kEmptyFileAlias;
  PrintString(name);
}

STDMETHODIMP CArchiveUpdateCallback::GetStream(UInt32 index, ISequentialInStream **inStream)
{
  // PRINTUNIFIED("GetStream invoked for index %u",index);
  RINOK(Finilize())

  const CDirItem &dirItem = (*DirItems)[index];
  GetStream2(dirItem.Name);

  if (dirItem.isDir())
    return S_OK;

  {
    auto inStreamSpec = new CInFileStream;
    CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
    FString path = DirPrefix + dirItem.FullPath;
    if (!inStreamSpec->Open(path))
    {
      DWORD sysError = ::GetLastError();
      FailedCodes.Add(sysError);
      FailedFiles.Add(path);
      // if (systemError == ERROR_SHARING_VIOLATION)
      {
        PrintNewLine();
        PrintError("WARNING: can't open file");
        // PrintString(NError::MyFormatMessageW(systemError));
        return S_FALSE;
      }
      // return sysError;
    }
    *inStream = inStreamLoc.Detach();
  }
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::SetOperationResult(Int32 /* operationResult */)
{
  m_NeedBeClosed = true;
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeSize(UInt32 index, UInt64 *size)
{
  if (VolumesSizes.Size() == 0)
    return S_FALSE;
  if (index >= (UInt32)VolumesSizes.Size())
    index = VolumesSizes.Size() - 1;
  *size = VolumesSizes[index];
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::GetVolumeStream(UInt32 index, ISequentialOutStream **volumeStream)
{
  wchar_t temp[16];
  ConvertUInt32ToString(index + 1, temp);
  UString res = temp;
  while (res.Len() < 2)
    res.InsertAtFront(L'0');
  UString fileName = VolName;
  fileName += L'.';
  fileName += res;
  fileName += VolExt;
  auto streamSpec = new COutFileStream;
  CMyComPtr<ISequentialOutStream> streamLoc(streamSpec);
  if (!streamSpec->Create(us2fs(fileName), false))
    return ::GetLastError();
  *volumeStream = streamLoc.Detach();
  return S_OK;
}

STDMETHODIMP CArchiveUpdateCallback::CryptoGetTextPassword2(Int32 *passwordIsDefined, BSTR *password)
{
  if (!PasswordIsDefined)
  {
    if (AskPassword)
    {
      // You can ask real password here from user
      // Password = GetPassword(OutStream);
      // PasswordIsDefined = true;
      PrintError("Password is not defined");
      return E_ABORT;
    }
  }
  *passwordIsDefined = BoolToInt(PasswordIsDefined);
  return StringToBstr(Password, password);
}


/***************************************************/
/********* ArchiveUpdateCallbackFromFd *************/
/***************************************************/

//struct PosixCompatDirItem {
//    // int fd; // fd received over UDS
//    struct stat st;
//    std::string filename;
//
//    PosixCompatDirItem(std::string& filename_, struct stat st_) :
//            filename(filename_), st(st_) /*, fd(-1)*/ {}
//};

// for debugging using python client
struct PosixCompatDirItem {
    // int fd; // fd received over UDS
    uint32_t st_mode;
    uint64_t st_size;
    uint64_t st_archivetime;
    uint64_t st_creationtime;
    uint64_t st_modificationtime;

    std::string filename;

//    PosixCompatDirItem(
//            std::string filename,
//            uint32_t stMode,
//            uint64_t stSize,
//            uint64_t stArchivetime,
//            uint64_t stCreationtime,
//            uint64_t stModificationtime) : filename(std::move(filename)),
//                                           st_mode(stMode),
//                                           st_size(stSize),
//                                           st_archivetime(stArchivetime),
//                                           st_creationtime(stCreationtime),
//                                           st_modificationtime(stModificationtime) {}
    explicit PosixCompatDirItem(std::string filename) : filename(std::move(filename)) {}
};

class ArchiveUpdateCallbackFromFd : public CArchiveUpdateCallback
{
public:

    // IUpdateCallback2
    STDMETHOD(GetProperty) (UInt32 index, PROPID propID, PROPVARIANT *value) override;
    STDMETHOD(GetStream) (UInt32 index, ISequentialInStream **inStream) override;
    STDMETHOD(GetVolumeSize) (UInt32 index, UInt64 *size) override;
    STDMETHOD(GetVolumeStream) (UInt32 index, ISequentialOutStream **volumeStream) override;

public:
    int udsNative;
    std::vector<PosixCompatDirItem> fstatsList;

    explicit ArchiveUpdateCallbackFromFd(IDescriptor* inOutDesc_) {
        inOutDesc = inOutDesc_;
        udsNative = (dynamic_cast<PosixDescriptor*>(inOutDesc))->desc;
    };

    /* Will read one fd at a time (i.e. when needed) from inOutDesc */

    // receive filenames and stats to be available to getProperty callback (fds not received yet at this stage)
    void receiveStats() {
        for(;;) {
            std::string filename = readStringWithLen(*inOutDesc);
            if(filename.empty()) break;
//            struct stat st{};
//            inOutDesc.readAllOrExit(&st,sizeof(struct stat));
//            fstatsList.emplace_back(PosixCompatDirItem(filename,st));

            // DEBUG: read necessary fields one at a time
            PosixCompatDirItem p(filename);
            inOutDesc->readAllOrExit(&(p.st_mode), sizeof(uint32_t));
            inOutDesc->readAllOrExit(&(p.st_size), sizeof(uint64_t));
            inOutDesc->readAllOrExit(&(p.st_archivetime), sizeof(uint64_t));
            inOutDesc->readAllOrExit(&(p.st_creationtime), sizeof(uint64_t));
            inOutDesc->readAllOrExit(&(p.st_modificationtime), sizeof(uint64_t));
            fstatsList.emplace_back(p);
        }
    }
};


// PGP
#if __RH_WORDSIZE__ == 32
auto UNIXTimestampToWindowsFILETIME = NWindows::NTime::UnixTimeToFileTime;
#elif __RH_WORDSIZE__ == 64
auto UNIXTimestampToWindowsFILETIME = NWindows::NTime::UnixTime64ToFileTime;
#else
#error Unable to detect ABI size
#endif
// PGP

STDMETHODIMP ArchiveUpdateCallbackFromFd::GetProperty(UInt32 index, PROPID propID, PROPVARIANT *value)
{
    // PRINTUNIFIED("GetProperty invoked for index %u",index);
    NCOM::CPropVariant prop;

    if (propID == kpidIsAnti)
    {
        prop = false;
        prop.Detach(value);
        return S_OK;
    }

    {
        auto& currentDirItem = fstatsList[index];
        FILETIME ft{};
        switch (propID) {
            case kpidPath:
                prop = UString(UTF8_to_wchar(currentDirItem.filename).c_str()); // TODO check: path or filename only?
                break;
            case kpidIsDir:
//                prop = S_ISDIR(currentDirItem.st.st_mode);
                prop = S_ISDIR(currentDirItem.st_mode);
                break;
            case kpidSize:
//                prop = (UInt64)(currentDirItem.st.st_size);
                prop = (UInt64)(currentDirItem.st_size);
                break;
            case kpidAttrib:
//                prop = currentDirItem.st.st_mode; // TODO likely wrong, posix attribs vs windows ones
                prop = currentDirItem.st_mode;
                break;
            case kpidCTime:
//                UNIXTimestampToWindowsFILETIME(currentDirItem.st.st_ctime,ft);
                UNIXTimestampToWindowsFILETIME(currentDirItem.st_creationtime,ft);
                prop = ft;
                break;
            case kpidATime:
//                UNIXTimestampToWindowsFILETIME(currentDirItem.st.st_atime,ft);
                UNIXTimestampToWindowsFILETIME(currentDirItem.st_archivetime,ft);
                prop = ft;
                break;
            case kpidMTime:
//                UNIXTimestampToWindowsFILETIME(currentDirItem.st.st_mtime,ft);
                UNIXTimestampToWindowsFILETIME(currentDirItem.st_modificationtime,ft);
                prop = ft;
                break;
        }
    }
    prop.Detach(value);
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallbackFromFd::GetStream(UInt32 index, ISequentialInStream **inStream)
{
    // By construction, here all the file stats item have already been received
    // PRINTUNIFIED("GetStream invoked for index %u",index);
    RINOK(Finilize())

    constexpr uint64_t maxuint = -1;
    
    inOutDesc->writeAllOrExit(&maxuint,sizeof(uint64_t)); // send EOF for previous file, so that client knows it has to send next fd

    auto& currentDirItem = fstatsList[index];

    GetStream2(UTF8_to_wchar(currentDirItem.filename).c_str());

//    if (S_ISDIR(currentDirItem.st.st_mode))
    if (S_ISDIR(currentDirItem.st_mode))
        return S_OK;

    {
        // TODO TO BE CHECKED - Assumption: getStream called once for each file, and NOT concurrently from different threads
        // send fd request using index and receive fd
        // PRINTUNIFIEDERROR("Sending index after EOF: %u",index);
        inOutDesc->writeAllOrExit(&index,sizeof(UInt32)); // inOutDesc and udsNative are actually the same descriptor
        // PRINTUNIFIEDERROR("Receiving fd for index: %u",index);
        int fdToBeReceived = recvfd(udsNative);
        auto inStreamSpec = new CInFdStream(fdToBeReceived);
        CMyComPtr<ISequentialInStream> inStreamLoc(inStreamSpec);
        *inStream = inStreamLoc.Detach();
    }
    return S_OK;
}

STDMETHODIMP ArchiveUpdateCallbackFromFd::GetVolumeSize(UInt32 index, UInt64 *size)
{
    return S_FALSE;
}

STDMETHODIMP ArchiveUpdateCallbackFromFd::GetVolumeStream(UInt32 index, ISequentialOutStream **volumeStream)
{
    return S_FALSE;
}

/***************************************************/
/********* ArchiveUpdateCallbackFromFd *************/
/***************************************************/

#endif /* __RH_ARCHIVE_UTILS_H__ */
#endif /* _WIN32 */
