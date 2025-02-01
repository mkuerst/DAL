#!/bin/sh

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

cd tmp
sudo apt-get -y --force-yes install cmake

# memcached
sudo apt-get -y --force-yes install memcached libmemcached-dev

# cityhash
git clone https://github.com/google/cityhash.git
cd cityhash
./configure
make all check CXXFLAGS="-g -O3"
sudo make install
cd ..

# boost
wget https://jaist.dl.sourceforge.net/project/boost/boost/1.53.0/boost_1_53_0.zip
unzip boost_1_53_0.zip
cd boost_1_53_0
./bootstrap.sh
# ./b2 install --with-system --with-coroutine --build-type=complete --layout=versioned threading=multi
./b2 install --with-system --with-coroutine --layout=versioned threading=multi
sudo apt-get -y --force-yes install libboost-all-dev
cd ..

# paramiko
sudo apt-get -y --force-yes install python3-pip
pip3 install --upgrade pip
pip3 install paramiko

# gdown
pip3 install gdown
pip3 install --upgrade --no-cache-dir gdown

# func_timeout
pip3 install func_timeout

# matplotlib
pip3 install matplotlib

# tbb
git clone https://github.com/wjakob/tbb.git
cd tbb/build
cmake ..
make -j
sudo make install
ldconfig
cd ../..

# openjdk-8
sudo apt-get -y --force-yes install openjdk-8-jdk

wget https://content.mellanox.com/ofed/MLNX_OFED-4.9-5.1.0.0/MLNX_OFED_LINUX-4.9-5.1.0.0-ubuntu22.04-x86_64.tgz
tar -xvf MLNX_OFED_LINUX-4.9-5.1.0.0-ubuntu22.04-x86_64.tgz
cd MLNX_OFED_LINUX-4.9-5.1.0.0-ubuntu22.04-x86_64

sudo ./mlnxofedinstall  --force
sudo /etc/init.d/openibd restart
sudo /etc/init.d/opensmd restart

cd ..
rm -rf tmp

sudo apt install -y rdma-core librdmacm-dev libibverbs-dev
sudo apt install -y libnuma-dev
sudo apt install -y gh
sudo apt-get install -y pdsh 

sudo sed -i 's/^#PubAuthentication no/PubAuthentication yes/' /etc/ssh/sshd_config 
sudo sed -i 's/^PubAuthentication no/PubAuthentication yes/' /etc/ssh/sshd_config 
sudo sed -i 's/^#AuthorizedKeysFile .ssh/authorized_keys .ssh/authorized_keys/AuthorizedKeysFile /users/mkuerst/.ssh/authorized_keys' /etc/ssh/sshd_config 
sudo sed -i 's/^AuthorizedKeysFile .ssh/authorized_keys .ssh/authorized_keys/AuthorizedKeysFile /users/mkuerst/.ssh/authorized_keys' /etc/ssh/sshd_config 

PUBKEY_FILE="/nfs/id_rsa.pub"
AUTHORIZED_KEYS="~/.ssh/authorized_keys"

sudo cp ~/id_rsa ~/.ssh/

sudo cat "$PUBKEY_FILE" >> "$AUTHORIZED_KEYS"
echo -e "Host *\n    StrictHostKeyChecking accept-new" >> ~/.ssh/config
sudo systemctl restart sshd