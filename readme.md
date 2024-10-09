# TCP Forwarder
is a SOCKS5 proxy server that support both IPV4 and IPV6 (domain support is in progress)

# Installation & run
```sh
git clone https://github.com/fadhil-riyanto/tcp-forwarder.git
cd tcp-forwarder
mkdir build
cd build
cmake ..
make -j4 
./tcpf --listen 8013 --addr 127.0.0.1
```

# License
GPL-2.0