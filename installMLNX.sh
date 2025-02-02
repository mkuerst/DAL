#!/bin/bash

# ofed-ubuntu.sh
# Install Mellanox Ofed on Ubuntu Machines
#
# Author: Nilson Lopes (06/17/2021)

ubuntu_release=$(awk -F '"' '/VERSION_ID/ {print $2}' /etc/os-release)
mofed_package_version='latest'
mofed_repo_base_url="https://linux.mellanox.com/public/repo/mlnx_ofed"
mofed_repo_gpg_url="https://www.mellanox.com/downloads/ofed/RPM-GPG-KEY-Mellanox"
ubuntu_repo_dist_name="ubuntu${ubuntu_release}"
ubuntu_repo_file_name="mellanox_mlnx_ofed.list"
ubuntu_repo_file_path="/etc/apt/sources.list.d/${ubuntu_repo_file_name}"

echo "adding apt key"
wget -qO - ${mofed_repo_gpg_url} | apt-key add -

url="${mofed_repo_base_url}/${mofed_package_version}/${ubuntu_repo_dist_name}/${ubuntu_repo_file_name}"
wget -qO ${ubuntu_repo_file_path} ${url}
chmod 644 ${ubuntu_repo_file_path}

echo "update apt cache"
apt update -y

echo "install packages"
OFED_PKGS=(libc6-dev
 mlnx-ofed-kernel-dkms
 mlnx-ofed-kernel-utils
 mlnx-ofed-basic
 rdma-core
 ibverbs-utils
 ibverbs-providers
)

apt install -y ${OFED_PKGS[@]}

echo "configure ib devices ..."
systemctl restart openibd