# beada
DRM Driver for BeadaPanel USB Media Display, inherit from main stream gm12u320 driver.

#### How to build
```
git clone https://github.com/JT365/beada 
cd beada/src
make -C /usr/src/linux-headers-`uname -r`/ M=`pwd` modules
```
