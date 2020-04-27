#ifndef __RH_SSH_KEYGEN_ED25519__
#define __RH_SSH_KEYGEN_ED25519__

#include <iostream>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <utility>
#include "../unifiedlogging.h"
#include "botan_all.h"

inline static void fatal(const char *msg) {
    PRINTUNIFIEDERROR("fatal: %s\n",msg);
    throw std::runtime_error(msg);
}

static void write_to_file(const char *filename, unsigned mode, const char *p, uint32_t size) {
#ifndef _WIN32
    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, mode);
    int got;
    if (fd < 0) fatal("file open for write");
    got = write(fd, p, size);
    if (got < 0 || got + 0U != size) fatal("write file");
    close(fd);
#else
    FILE* f = fopen(filename, "wb");
    if (f == nullptr) fatal("file open for write");
    auto got = fwrite(p,1,size,f);
    if (got < 0 || got + 0U != size) fatal("write file");
    fclose(f);
#endif
}

static std::vector<uint8_t> uint32AsBigEndian(uint32_t num) {
    //Store big endian representation in a vector:
    std::vector<uint8_t> bigEndian;
    for(int i=3; i>=0; i--)
        bigEndian.push_back((num>>(8*i)) & 0xff);
    return bigEndian;
}

static std::string getBase64StringWithNewlines(const std::string& s) {
	std::stringstream sstream;
	for(int i=0;i<s.size();i++) {
		sstream<<s[i];
		if(i!=0 && i%70==69) sstream<<'\n';
	}
	auto&& s1 = sstream.str();
	if(s1[s1.size()-1]=='\n') return s1;
	else return s1+"\n";
}

static std::pair<std::string,std::string> generate_ed25519_keypair(const char *filename, const std::string& comment) {
    Botan::AutoSeeded_RNG rng;
    Botan::Ed25519_PrivateKey k(rng);
    auto& prvk = k.get_private_key();
    auto& pubk = k.get_public_key();
    auto&& checkstr = rng.random_vec(4);
    
    // DEBUG
    //~ std::vector<uint8_t> prvForFile(prvk.data(),prvk.data()+prvk.size());
    //~ prvForFile.resize(32);
    //~ std::vector<uint8_t> pubForFile(pubk.data(),pubk.data()+pubk.size());

    //~ std::ofstream fout_prv("/sdcard/ed25519_prv.raw",std::ios::binary);
    //~ fout_prv.write((const char*)prvForFile.data(),prvForFile.size());
    //~ fout_prv.close();

    //~ std::ofstream fout_pub("/sdcard/ed25519_pub.raw",std::ios::binary);
    //~ fout_pub.write((const char*)pubForFile.data(),pubForFile.size());
    //~ fout_pub.close();

    //~ std::ofstream fout_chk("/sdcard/ed25519_check.raw",std::ios::binary);
    //~ fout_chk.write((const char*)checkstr.data(),checkstr.size());
    //~ fout_chk.close();

    //~ std::ofstream fout_cmt("/sdcard/ed25519_comment.raw");
    //~ fout_cmt.write(comment.data(),comment.size());
    //~ fout_cmt.close();
    // DEBUG

    if(prvk.size() != 64 || pubk.size() != 32) {
		PRINTUNIFIEDERROR("Private key must be 64 bytes long, public key 32 bytes, actual prvk size: %u\tactual pubk size: %u\n",prvk.size(),pubk.size());
		throw std::runtime_error("Keys size mismatch");
    }
    
    std::vector<uint8_t> prvKeyOnly(prvk.data(),prvk.data()+prvk.size());
    prvKeyOnly.resize(32);

    // build private key ******************************
    std::string privkHeader = "-----BEGIN OPENSSH PRIVATE KEY-----\n";
    std::string privkFooter = "-----END OPENSSH PRIVATE KEY-----\n";

    std::vector<uint8_t> innerPrivkHeader_0 {'o','p','e','n','s','s','h','-','k','e','y','-','v','1',
                                0,0,0,0,4,
                                'n','o','n','e',
                                0,0,0,4,
                                'n','o','n','e',
                                0,0,0,0,0,0,0,1,0,0,0,0x33
    };
    std::vector<uint8_t> innerPrivkHeader_1 {0,0,0,0x0B,'s','s','h','-','e','d','2','5','5','1','9',0,0,0,' '};
    std::vector<uint8_t> innerPrivkHeader_2 {0,0,0,'@'};

    std::vector<uint8_t> commentBytes((uint8_t*)comment.c_str(),(uint8_t*)(comment.c_str()+comment.size()));

    std::vector<uint8_t> finalInnerHeader {1,2,3,4,5,6,7};
    size_t xx = (commentBytes.size() + 3) & 7;
    if(xx == 0) finalInnerHeader = {};
    else finalInnerHeader.resize(8-xx); // take first bytes

    if(std::find(commentBytes.begin(),commentBytes.end(),'\n')!=commentBytes.end())
        throw std::runtime_error("Comment contains newlines");

    std::vector<uint8_t> beforeB64; // TODO replace with botan secure vector
    beforeB64.reserve(1024);
    beforeB64.insert(beforeB64.end(),innerPrivkHeader_0.begin(),innerPrivkHeader_0.end());
    beforeB64.insert(beforeB64.end(),innerPrivkHeader_1.begin(),innerPrivkHeader_1.end());
    beforeB64.insert(beforeB64.end(),pubk.begin(),pubk.end());

    uint32_t paddedCommentSize = 131 + commentBytes.size() + (-(commentBytes.size() + 3) & 7);
    auto&& paddedCommentSizeBytes = uint32AsBigEndian(paddedCommentSize);
    beforeB64.insert(beforeB64.end(),paddedCommentSizeBytes.begin(),paddedCommentSizeBytes.end());

    beforeB64.insert(beforeB64.end(),checkstr.begin(),checkstr.end());
    beforeB64.insert(beforeB64.end(),checkstr.begin(),checkstr.end());
    beforeB64.insert(beforeB64.end(),innerPrivkHeader_1.begin(),innerPrivkHeader_1.end());
    beforeB64.insert(beforeB64.end(),pubk.begin(),pubk.end());
    beforeB64.insert(beforeB64.end(),innerPrivkHeader_2.begin(),innerPrivkHeader_2.end());

    // TODO can be replaced with the entire keypair object from Botan
    beforeB64.insert(beforeB64.end(),prvKeyOnly.begin(),prvKeyOnly.end());
    beforeB64.insert(beforeB64.end(),pubk.begin(),pubk.end());
    
    auto&& commentLenAsBigEndian = uint32AsBigEndian(commentBytes.size());
    beforeB64.insert(beforeB64.end(),commentLenAsBigEndian.begin(),commentLenAsBigEndian.end());

    beforeB64.insert(beforeB64.end(),commentBytes.begin(),commentBytes.end());
    beforeB64.insert(beforeB64.end(),finalInnerHeader.begin(),finalInnerHeader.end());

    auto&& afterB64 = Botan::base64_encode(beforeB64);
    // PRINTUNIFIED("afterB64:****\n%s\n*********\n",afterB64.c_str());
    auto&& splittedB64Content = getBase64StringWithNewlines(afterB64);

    // build public key ******************************
    // concat with public key BEFORE base64 encoding
    std::vector<uint8_t> innerHeader {0,0,0,0x0B,'s','s','h','-','e','d','2','5','5','1','9',0,0,0,' '};
    innerHeader.insert(innerHeader.end(),pubk.begin(),pubk.end());
    std::string b64encoded = Botan::base64_encode(innerHeader); // TODO check if padding is necessary (checked, shouldn't be)
    std::string sshpubkey = "ssh-ed25519 "+b64encoded+" "+comment+"\n";
    std::string sshprvkey = privkHeader+splittedB64Content+privkFooter;
    
    if(filename != nullptr) {
		const std::string prvkName = filename;
		const std::string pubkName = prvkName + ".pub";
		
		write_to_file(prvkName.c_str(), 0600, sshprvkey.c_str(), sshprvkey.size());
		write_to_file(pubkName.c_str(), 0644, sshpubkey.c_str(), sshpubkey.size());
		return {};
	}

    return std::make_pair(sshprvkey,sshpubkey);
}

#endif /* __RH_SSH_KEYGEN_ED25519__ */
