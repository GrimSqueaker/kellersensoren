[![Language](https://img.shields.io/badge/language-C++-blue.svg)](https://isocpp.org/)
[![Standard](https://img.shields.io/badge/C%2B%2B-11-blue.svg)](https://en.wikipedia.org/wiki/C%2B%2B#Standardization)


This projects uses wiringPi to constantly read temperature and humidity data from three DHT11 sensors.
Afterwards it computes the dewpoint and sends the data to an openHAB server via libcurl.

# Building and installation

```bash
mkdir build
cd build
conan install -s build_type=Release ../conanfile.txt
cmake ..
make 
sudo make install
```

# Installation as systemd service

```bash
sudo cp scripts/kellersensoren.service /etc/systemd/system
sudo systemctl enable kellersensoren
sudo systemctl start kellersensoren
```
