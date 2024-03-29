#ifndef _WIN32
#error "Windows required for windiriterator"
#endif

#ifndef WINDIRITERATOR_H
#define WINDIRITERATOR_H

#include "IdirIterator.h"

class windirIterator: public IDirIterator<std::wstring> {
private:
    static constexpr const wchar_t* systemPathSeparator = L"\\";

    std::stack<std::wstring> S;
    std::stack<std::wstring>* NamesOnly; // auxiliary stack with same ordering of S, where filenames are pushed only where in NAME mode
    size_t rootLen;

    std::wstring getParentDir(std::wstring child) {
        if (((child[0] >= L'A' && child[0] <= L'Z') || (child[0] >= L'a' && child[0] <= L'z')) && child[1]==L':') return L"";
        ssize_t last = child.find_last_of(systemPathSeparator);
        if (last < 0) return L""; // FIXME ambiguity on windows (PC root path VS error)
        return child.substr(0, last);
    }

    bool listOnStack(const std::wstring& dir) {
        WIN32_FIND_DATAW fdFile;
        HANDLE hFind = nullptr;

        //Specify a file mask. *.* = We want everything!
        std::wstring sPath = dir + L"\\*.*";

        if((hFind = FindFirstFileW(sPath.c_str(), &fdFile)) == INVALID_HANDLE_VALUE) {
            std::wcout<<L"Path not found: "<<dir<<std::endl;
            return false;
        }

        if(sortCurrentLevelByName) {
            // sort current listing and add the sorted array to the stack(s); used by directory hashing
            std::vector<std::pair<std::wstring,std::wstring>> currentLevel;
            do {
                //Find first file will always return "."
                //    and ".." as the first two directories.
                if(wcscmp(fdFile.cFileName, L".") != 0
                   && wcscmp(fdFile.cFileName, L"..") != 0) {
                    //Build up our file path using the passed in
                    //  [dir] and the file/foldername we just found:

                    std::wstring currentFilePath = dir + L"\\" + fdFile.cFileName;
                    std::wstring currentFileName = fdFile.cFileName;

                    // Plain, no recursion
                    // std::wcout<<L"Current file path: "<<currentFilePath<<L"\tCurrent filename: "<<currentFileName<<std::endl;
                    
                    if(filterEnabled) {
                        auto&& cf = TOUTF(currentFileName);
                        if(!std::regex_match(cf.c_str(),filterMatch,filter)) continue;
                    }
                    currentLevel.emplace_back(currentFilePath,currentFileName);
                    // S.push(currentFilePath);
                    // if (provideFilenames) NamesOnly->push(currentFileName);
                }
            }
            while(FindNextFileW(hFind, &fdFile)); //Find the next file.
            if(currentLevel.size()==0) currentEfd = '@'; // BEWARE!!! will consider empty also a FILTERED empty dir
            else {
                std::sort(currentLevel.begin(),currentLevel.end(),IDirIterator::defaultPairComparator);
                for(auto& pair: currentLevel) {
                    S.push(pair.first);
                    if(provideFilenames) NamesOnly->push(pair.second);
                }
            }
        }
        else {
            bool dirEmpty = true;
            do {
                //Find first file will always return "."
                //    and ".." as the first two directories.
                if(wcscmp(fdFile.cFileName, L".") != 0
                   && wcscmp(fdFile.cFileName, L"..") != 0) {
                    //Build up our file path using the passed in
                    //  [dir] and the file/foldername we just found:

                    std::wstring currentFilePath = dir + L"\\" + fdFile.cFileName;
                    std::wstring currentFileName = fdFile.cFileName;

                    // Plain, no recursion
                    // std::wcout<<L"Current file path: "<<currentFilePath<<L"\tCurrent filename: "<<currentFileName<<std::endl;
                    if(filterEnabled) {
                        auto&& cf = TOUTF(currentFileName);
                        if(!std::regex_match(cf.c_str(),filterMatch,filter)) continue;
                    }
                    S.push(currentFilePath);
                    if (provideFilenames) NamesOnly->push(currentFileName);
                    dirEmpty = false; // behaviour aligned to sorted branch
                }
            }
            while(FindNextFileW(hFind, &fdFile)); //Find the next file.
            if(dirEmpty) currentEfd = '@';
        }

        FindClose(hFind); //Always, Always, clean things up!
        return true;
    }

public:
    windirIterator(std::wstring& dir_,
                   IterationMode mode_,
                   bool provideFilenames_ = true,
                   ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS,
                   bool sortCurrentLevelByName_ = false,
                   std::string filterPattern_ = "") :
            IDirIterator(dir_,mode_,provideFilenames_,recursiveListing_,sortCurrentLevelByName_,std::move(filterPattern_)) { // TODO discriminate between RECURSIVE and RECURSIVE_FOLLOW_SYMLINKS

        if(provideFilenames) NamesOnly = new std::stack<std::wstring>();

        if (dir.empty()) { // list logical drives
            std::cerr<<"Logical drives to be listed by caller for now"<<std::endl;
            error = errno = EINVAL;
            return;
        }

        std::wstring parentDir;
        switch (mode) {
            case RELATIVE_INCL_BASE:
                parentDir = getParentDir(dir);
                // std::cout<<"parent dir is "<<parentDir<<std::endl;
                rootLen = parentDir.length() + 1;
                break;
            case RELATIVE_WITHOUT_BASE:
                rootLen = dir.length() + 1;
                break;
                // FULL and NAME just as guard blocks
            case FULL:
                rootLen = 0;
                break;
            default:
                std::cerr<<"invalid enum value for dir iteration mode\n";
                error = errno = EINVAL;
                return;
        }

        if (!listOnStack(dir)) error = GetLastError();
    }

    ~windirIterator() {
        if (provideFilenames) delete NamesOnly;
    }

    bool next() {
        if (S.empty()) return false;

        current = S.top();
        S.pop();
        if (provideFilenames) {
            currentName = NamesOnly->top();
            NamesOnly->pop();
        }

        char efd = IDirIterator::efdL(current);
        currentEfd = efd;
        if (efd == 'd' && (recursiveListing == RECURSIVE_FOLLOW_SYMLINKS || recursiveListing == RECURSIVE)) {
            // folder
            listOnStack(current); // TODO bool check
        }

        switch (mode) {
            case RELATIVE_WITHOUT_BASE:
            case RELATIVE_INCL_BASE:
                current = current.substr(rootLen);
                break;
            case FULL:
                break;
            default:
                throw std::runtime_error("invalid enum value for dir iteration mode");
        }
        return true;
    }

    std::wstring getCurrentFilename() { // the name only of the return path of a next() call
        return currentName;
    }

    std::wstring getCurrent() {
        return current;
    }
};

class windirIteratorFactory {
public:
    windirIterator createIterator(std::wstring dir_,IterationMode mode_,
                                  bool provideFilenames_ = true,
                                  ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS,
                                  bool sortCurrentLevelByName_ = false,
                                  std::string filterPattern_ = "") {
        return {dir_,mode_,provideFilenames_,recursiveListing_,sortCurrentLevelByName_,std::move(filterPattern_)};
    }
};

#endif /* WINDIRITERATOR_H */
