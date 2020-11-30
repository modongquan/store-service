#!/bin/bash

root_path=/mnt/teamway/store-service
grpc_path=${root_path}/third/grpc/install/bin
grpc_src_path=${root_path}/src/grpc

export LD_LIBRARY_PATH=${grpc_path}/../lib

${grpc_path}/protoc -I . --grpc_out=. --plugin=protoc-gen-grpc=`which ${grpc_path}/grpc_cpp_plugin` gb28181.proto
${grpc_path}/protoc -I . --cpp_out=. gb28181.proto
