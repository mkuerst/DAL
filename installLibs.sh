#!/bin/sh

if [ ! -d "tmp" ]; then
	mkdir tmp
fi

cd tmp



OFED_FILE="$HOME/ready.txt"
if [ ! -f "$OFED_FILE" ]; then
    echo "MLNX OFED version is not installed. Running installation script..."
        sudo apt-get -y --force-yes install cmake

        sudo apt-get -y install linux-tools-common linux-tools-generic linux-tools-`uname -r`
        sudo apt-get -y install libpapi-dev

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

        sudo apt-get -y --force-yes install python3-pip
        pip3 install --upgrade pip
        pip3 install paramiko

        pip3 install gdown
        pip3 install --upgrade --no-cache-dir gdown

        pip3 install func_timeout
        pip3 install matplotlib

        tbb
        git clone https://github.com/wjakob/tbb.git
        cd tbb/build
        cmake ..
        make -j
        sudo make install
        ldconfig
        cd ../..

        openjdk-8
        sudo apt-get -y --force-yes install openjdk-8-jdk

        cd /local/repository/

        sudo apt install -y rdma-core librdmacm-dev libibverbs-dev
        sudo apt-get install -y infiniband-diags
        sudo apt install -y libnuma-dev
        sudo apt install -y gh
        sudo apt install -y pdsh 
        sudo apt install -y clustershell
        sudo apt install -y htop
        sudo apt install -y dsh
        sudo apt install -y libpapi-dev papi-tools

        sudo apt remove --purge python3.8
        sudo apt autoremove
        sudo apt update
        sudo apt install -y python3.9 python3.9-venv python3.9-dev python3.9-distutils
        curl -sS https://bootstrap.pypa.io/get-pip.py | python3.9
        export PATH="/usr/local/bin/python3.9:$PATH"
        pip3 install matplotlib
        pip3 install pandas


		sudo bash installMLNX.sh
else
    echo "MLNX OFED version already installed."
fi

sudo apt install -y openmpi-bin openmpi-common libopenmpi-dev

sudo chown -R mkuerst:dal-PG0 $HOME
sudo touch $HOME/ready.txt