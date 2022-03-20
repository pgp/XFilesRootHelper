# Building a universal linux XFilesRootHelper executable

These are the steps needed to build a universal release of RootHelper that runs on most linux distributions.

The build environment is based on CentOS 6.10 x64, the oldest currently-supported linux distro.
For the purpose you can install this OS in a virtual machine, or use a Docker image, for example:

```sh
docker pull centos:6.10
docker run -it --entrypoint "/bin/bash" --name centosbuild centos:6.10 -i
```

### Use CentOS Vault repos for yum
```sh
sudo -s # not needed within Docker container
echo "https://vault.centos.org/6.10/os/x86_64/" > /var/cache/yum/x86_64/6/base/mirrorlist.txt
echo "http://vault.centos.org/6.10/extras/x86_64/" > /var/cache/yum/x86_64/6/extras/mirrorlist.txt
echo "http://vault.centos.org/6.10/updates/x86_64/" > /var/cache/yum/x86_64/6/updates/mirrorlist.txt
echo "http://vault.centos.org/6.10/sclo/x86_64/rh" > /var/cache/yum/x86_64/6/centos-sclo-rh/mirrorlist.txt
echo "http://vault.centos.org/6.10/sclo/x86_64/sclo" > /var/cache/yum/x86_64/6/centos-sclo-sclo/mirrorlist.txt
```

### Install toolchain and dependencies

```sh
sudo -s # not needed within Docker container
yum update
yum remove java-1.7.0-openjdk
yum install centos-release-scl
yum install devtoolset-8
yum install nano git curl wget openssl openssl-devel

# download a python 3 runtime (pypy3), since centos 6 does not provide recent python3 packages
cd /opt
wget https://downloads.python.org/pypy/pypy3.6-v7.3.3-linux64.tar.bz2
tar xf pypy3.6-v7.3.3-linux64.tar.bz2
rm -f pypy3.6-v7.3.3-linux64.tar.bz2

# download and build a recent cmake version
cd /tmp
wget https://www.cmake.org/files/v3.6/cmake-3.6.0.tar.gz

tar xf cmake-3.6.0.tar.gz
cd cmake-3.6.0
scl enable devtoolset-8 -- bash
./configure
make -j4

# if not using Docker, do without scl toolset enabled - in a new shell, or ctrl-D on the current - otherwise a syntax error is thrown
sudo make install
rm -rf cmake-3.6.0*
```

At this point, if using Docker, you can also convert the current container in an image for later reuse in future builds:
```sh
# from outside Docker
docker ps -a
CONTAINER_ID=5a46c0181737 # TODO replace with actual container id from previous command
docker commit $CONTAINER_ID centos6-base
```

### Build RootHelper

```sh
# from within container, if using Docker
cd /tmp
scl enable devtoolset-8 -- bash
git clone --recursive https://github.com/randombit/botan
git clone --recursive https://github.com/pgp/XFilesRootHelper

cd botan
git checkout tags/2.19.1
/opt/pypy3.6-v7.3.3-linux64/bin/pypy3 configure.py --amalgamation --without-os-feature=getauxval --disable-modules=pkcs11,tls_10 --cpu=x64 --os=linux --cc=gcc
mv -f botan_all.* ../XFilesRootHelper/botanAm/desktop/linux/x86_64

cd ../XFilesRootHelper
./build.sh -f

```

The resulting binary has been tested on the following systems:
- Ubuntu 12.04, 14.04, 16.04, 18.04, 20.04
- Mint 19, 20
- CentOS 6,7,8
- ArchLinux
