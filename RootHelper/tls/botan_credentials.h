/*
* (C) 2014,2015 Jack Lloyd
*
* Botan is released under the Simplified BSD License (see license.txt)
*/

#ifndef EXAMPLE_CREDENTIALS_MANAGER_H__
#define EXAMPLE_CREDENTIALS_MANAGER_H__


#include "botan_all.h"
#include <fstream>
#include <memory>
#include "../unifiedlogging.h"

inline bool value_exists(const std::vector<std::string>& vec,
                         const std::string& val)
{
   for(size_t i = 0; i != vec.size(); ++i)
      if(vec[i] == val) return true;
   return false;
}

class Basic_Credentials_Manager : public Botan::Credentials_Manager
{
public:
    Basic_Credentials_Manager()
    {
       load_certstores();
    }

    Basic_Credentials_Manager(Botan::RandomNumberGenerator& rng,
                              const std::string& server_crt,
                              const std::string& server_key)
    {
       Certificate_Info cert;

       cert.key.reset(Botan::PKCS8::load_key(server_key, rng));

       Botan::DataSource_Stream in(server_crt);
       while(!in.end_of_data())
       {
          try
          {
             cert.certs.emplace_back(Botan::X509_Certificate(in));
          }
//          catch(std::exception& e) {
          catch(...) {
//              PRINTUNIFIEDERROR("exception in reading certificates: %s\n",e.what());
//              exit(2749);
          }
       }

       // TODO attempt to validate chain ourselves

       m_creds.push_back(cert);
    }

    void load_certstores()
    {
       try
       {
#ifdef _WIN32
           // works also on MinGW starting with randombit/botan@cb6f4c4
           std::shared_ptr<Botan::Certificate_Store> cs(new Botan::Certificate_Store_Windows());
           m_certstores.push_back(cs);
#elif defined(__APPLE__)
           std::shared_ptr<Botan::Certificate_Store> cs(new Botan::Certificate_Store_MacOS());
           m_certstores.push_back(cs);
#else

#ifdef ANDROID_NDK
           const std::vector<std::string> paths { "/system/etc/security/cacerts" };
#elif defined(__linux__)
           const std::vector<std::string> paths { "/etc/ssl/certs", "/usr/share/ca-certificates" };
#else /* BSD */
           const std::vector<std::string> paths { "/usr/local/share/certs" };
#endif
           for(auto&& path : paths) {
               std::shared_ptr<Botan::Certificate_Store> cs(new Botan::Certificate_Store_In_Memory(path));
               m_certstores.push_back(cs);
           }

#endif
       }
       catch(std::exception& e) {
           PRINTUNIFIEDERROR("exception in loading certificates: %s\n",e.what());
           exit(2750);
       }
    }

    std::vector<Botan::Certificate_Store*>
    trusted_certificate_authorities(const std::string& type,
                                    const std::string& /*hostname*/) override
    {
       std::vector<Botan::Certificate_Store*> v;

       // don't ask for client certs
       if(type == "tls-server")
          return v;

       for(auto&& cs : m_certstores)
          v.push_back(cs.get());

       return v;
    }

    std::vector<Botan::X509_Certificate> cert_chain(
            const std::vector<std::string>& algos,
            const std::string& type,
            const std::string& hostname) override
    {
       BOTAN_UNUSED(type);

       for(auto&& i : m_creds)
       {
          if(std::find(algos.begin(), algos.end(), i.key->algo_name()) == algos.end())
             continue;

          if(!hostname.empty() && !i.certs[0].matches_dns_name(hostname))
             continue;

          return i.certs;
       }

       return std::vector<Botan::X509_Certificate>();
    }

    Botan::Private_Key* private_key_for(const Botan::X509_Certificate& cert,
                                        const std::string& /*type*/,
                                        const std::string& /*context*/) override
    {
       for(auto&& i : m_creds)
       {
          if(cert == i.certs[0])
             return i.key.get();
       }

       return nullptr;
    }

private:
    struct Certificate_Info
    {
        std::vector<Botan::X509_Certificate> certs;
        std::shared_ptr<Botan::Private_Key> key;
    };

    std::vector<Certificate_Info> m_creds;
    std::vector<std::shared_ptr<Botan::Certificate_Store>> m_certstores;
};

#endif
