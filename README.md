# beada
DRM Driver for BeadaPanel USB Media Display, inherit from up stream gm12u320 driver.

<img src="https://github.com/JT365/beada/blob/main/uds-2.png" width="600"/><br>

#### How to build
```
git clone -b rpi-6.6.y https://github.com/JT365/beada 
cd beada/src
make -C /usr/src/linux-headers-`uname -r`/ M=`pwd` modules
```
#### Installation
<https://elinux.org/BeadaPanel#DRM_Driver_Installation_on_Raspberry_Pi>
