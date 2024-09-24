project(tcp-forwarder)

add_executable(tcpf main.c)
add_library(tcpf submodule/log.c-patched/src/log.c)