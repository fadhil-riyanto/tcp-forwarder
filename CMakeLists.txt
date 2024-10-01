project(tcp-forwarder)

add_executable(tcpf main2.c)
add_library(tcpf submodule/log.c-patched/src/log.c)
target_compile_options(tcpf PRIVATE -O0 -g)