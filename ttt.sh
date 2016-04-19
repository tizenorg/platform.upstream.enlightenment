set -x

sudo apt-get install build-essential fakeroot dpkg-dev
mkdir ~/git-openssl
cd ~/git-openssl
apt-get source git
sudo apt-get build-dep git
sudo apt-get install libcurl4-openssl-dev
dpkg-source -x git_1.7.9.5-1.dsc
cd git-1.7.9.5


