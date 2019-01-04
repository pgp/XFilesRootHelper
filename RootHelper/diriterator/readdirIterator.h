#ifndef READDIRITERATOR_H
#define READDIRITERATOR_H

#include <iostream>
#include <string>
#include <cstring>
#include <cerrno>
#include <stdexcept>
#include <cstdlib>
#include <sstream>
#include <stack>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include "IdirIterator.h"

class readdirIterator: public IDirIterator<std::string> {
private:
    std::stack<std::string> S;
    std::stack<std::string>* NamesOnly; // auxiliary stack with same ordering of S, where filenames are pushed only where in NAME mode
    size_t rootLen;

    std::string getParentDir(std::string child) {
        size_t last = child.find_last_of("/");
        return child.substr(0, last);
    }

    // 0: not exists or access error
    // 1: existing file
    // 'd': existing directory
    // 'L': existing symlink to existing directory
    char efdL(const std::string& filepath) {
        struct stat st{};
        if (lstat(filepath.c_str(), &st) < 0) return 0; // non-existing or non-accessible
        if (S_ISLNK(st.st_mode)) {
			memset(&st,0,sizeof(struct stat));
			if (stat(filepath.c_str(), &st) < 0) return 0;
			if (S_ISDIR(st.st_mode)) return 'L'; // symlink to directory
			return 1; // symlink to file
		}
		else if (S_ISDIR(st.st_mode)) return 'd'; // actual directory
        else return 1;
    }
    
    /* 
	Smart visit dir symlinks:
		Every time a symlink is resolved, we have
		(L, T, R) = link, target, root
		If L descendant of T (infinite recursion) or T descendant of R (one more useless visit in the worst case), ignore; else:
		-- R = maxCommonPrefix(T,R), then visit R -- FIXME this makes no sense, recheck!
	*/
	/* UPDATED
	 * L child of T -> NO (infinite expansion)
	 * T child of R -> OK (at most duplicated expansion)
	 * T parent of R or equal to R -> NO (infinite expansion)
	 */
	// true: visit, false: don't visit
	bool smartVisitSymlink(const std::string& link, const char& efdL) {
//        PRINTUNIFIED("In smartVisitSymlink for %s, val %c\n",link.c_str(),efdL);
		if (efdL == 'd') {
//            PRINTUNIFIED("smartVisitSymlink d for val %c: ret true\n",efdL);
            return true;
        }
		else if (efdL == 'L') {
//            PRINTUNIFIED("smartVisitSymlink L for val %c\n",efdL);
			std::string target(65536,0);
            int rlret = readlink(link.c_str(),(char*)target.c_str(),65536);
			if (rlret < 0) {
//                PRINTUNIFIEDERROR("Unsolvable link error, don't visit\n");
                return false;
            } // unsolvable symlink, don't visit it
            target.resize(rlret);

//            PRINTUNIFIED("LINK %s VS TARGET %s\n",link.c_str(),target.c_str());
            if (link.compare(0,target.size(),target) == 0) {
//                PRINTUNIFIED("FOUND LINK %s TARGET %s\n",link.c_str(),target.c_str());
                return false;
            } // link path starts with target path => link is a descendant of its target
		
			// actually, we don't know if every path is listable in direct visit of the target path in this case
			// (it may as well could be accessible only through the symlink)
//            PRINTUNIFIED("ROOT %s VS TARGET %s\n",dir.c_str(),target.c_str());
            if (dir.compare(0,target.size(),target) == 0) {
//                PRINTUNIFIED("FOUND ROOT %s TARGET %s\n",dir.c_str(),target.c_str());
                return false;
            } // target path is parent or equal to root, don't visit
//            PRINTUNIFIED("FOUND VISITABLE\n");
			return true;
		}
		else return false;
	}

public:
    readdirIterator(std::string& dir_,
                    IterationMode mode_,
                    bool provideFilenames_ = true,
                    ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) :
            IDirIterator(dir_,mode_,provideFilenames_,recursiveListing_) {

        if(provideFilenames) NamesOnly = new std::stack<std::string>();

        if (!(dir.length()>0 && dir.at(0) == '/'))
            throw std::runtime_error("invalid dir path, only absolute paths allowed");

        std::string parentDir;
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
                throw std::runtime_error("invalid enum value for dir iteration mode");
        }

        struct dirent *d;
        auto dx = dir.c_str();
        errno = 0;
        DIR *Cdir_ = opendir(dx);
        if (Cdir_ == nullptr) {
			PRINTUNIFIEDERROR("Error opening dir %s for listing\n",dx);
			openError = true;
			return;
		}
        
        while ((d = readdir(Cdir_)) != nullptr) {
            // exclude current (.) and parent (..) directory
            if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
                continue;
            std::stringstream ss;
            ss<<dir<<"/"<<d->d_name; // path concat
            // PRINTUNIFIED("+ adding to stack: %s\n",ss.str().c_str());
            auto cItem_ = ss.str();
            S.push(cItem_);
            if (provideFilenames) NamesOnly->push(std::string(d->d_name));
        }
        closedir(Cdir_);
    }

    ~readdirIterator() {
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
        
        char efd = efdL(current);
        //~ if (efd == 2 && recursiveListing) { // LEGACY, always solves symlinks
        if ((recursiveListing == RECURSIVE_FOLLOW_SYMLINKS && (efd == 'd' || efd == 'L')) || 
			(recursiveListing == RECURSIVE && efd == 'd') || 
			(recursiveListing == SMART_SYMLINK_RESOLUTION && (efd == 'd' || efd == 'L') && smartVisitSymlink(current,efd))) {
            // folder
            struct dirent *d;
            errno = 0;
            DIR *dir_ = opendir(current.c_str());
            if (dir_ == nullptr) {
				int err___ = errno;
				PRINTUNIFIEDERROR("Error opening dir %s for listing, content won't be available during iteration, errno is %d\n",current.c_str(),err___);
			}
			else {
            while ((d = readdir(dir_)) != nullptr) {
                // exclude current (.) and parent (..) directory
                if (strcmp(d->d_name, ".") == 0 || strcmp(d->d_name, "..") == 0)
                    continue;
                std::stringstream ss;
                ss<<current<<"/"<<d->d_name; // path concat
                // PRINTUNIFIED("+ adding to stack: %s\n",ss.str().c_str());
                S.push(ss.str());
                if (provideFilenames) NamesOnly->push(std::string(d->d_name));
            }
            closedir(dir_);
			}
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

    std::string getCurrentFilename() { // the name only of the return path of a next() call
        return currentName;
    }

    std::string getCurrent() {
        return current;
    }
};

class readdirIteratorFactory : public IDirIteratorFactory<std::string> {
public:
    std::unique_ptr<IDirIterator<std::string>> createIterator(std::string dir_,
                                                 IterationMode mode_,
                                                 bool provideFilenames_ = true,
                                                 ListingMode recursiveListing_ = RECURSIVE_FOLLOW_SYMLINKS) {
        IDirIterator<std::string>* it = new readdirIterator(dir_,mode_,provideFilenames_,recursiveListing_);
        if (it->openError) {
			delete it;
			// web source: https://stackoverflow.com/questions/32204554/trying-to-return-a-stdunique-ptr-constructed-with-null
			return std::unique_ptr<IDirIterator<std::string>>(nullptr); // caller will check errno
		}
        return std::unique_ptr<IDirIterator<std::string>>(it);

        // causes SEGFAULT, destructor called immediately?
//        readdirIterator it(dir_,mode_,provideFilenames_,recursiveListing_);
//        return std::make_unique<readdirIterator>(it);

    }
};

#endif /* READDIRITERATOR_H */
