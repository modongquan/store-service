#!/bin/bash

root_path=/mnt/teamway/store-service
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:${root_path}/libs
grpc_path=${root_path}/inc/grpc
include_path=${root_path}/inc
cflag="-std=c++17"
CXX=g++
cd src

${CXX} ${cflag} -c comon.cpp -o comon.o
${CXX} ${cflag} -c mysql.cpp -o mysql.o
${CXX} ${cflag} -c store.cpp -o store.o
${CXX} ${cflag} -c stream.cpp -o stream.o -I${include_path}
${CXX} ${cflag} -c server.cpp -o server.o
${CXX} ${cflag} -c http.cpp -o http.o
${CXX} ${cflag} -c cJSON.cpp -o cJSON.o
# ${CXX} -c rtmpsrv.cpp -o rtmpsrv.o
# ${CXX} -c thread.cpp -o thread.o
${CXX} ${cflag} -c media.cpp -o media.o -I${include_path}
${CXX} ${cflag} -c monitor.cpp -o monitor.o
${CXX} ${cflag} -c backup.cpp -o backup.o
${CXX} ${cflag} -c grpc/gb28181.grpc.pb.cc -o gb28181.grpc.pb.o -I${grpc_path}
${CXX} ${cflag} -c grpc/gb28181.pb.cc -o gb28181.pb.o -I${grpc_path}
${CXX} ${cflag} -c grpc/grpc_client.cc -o grpc_client.o -I${grpc_path}
${CXX} ${cflag} -c main.cpp -o main.o
${CXX} main.o mysql.o store.o stream.o server.o http.o cJSON.o media.o monitor.o backup.o gb28181.grpc.pb.o gb28181.pb.o grpc_client.o comon.o -o main \
-lmysqlclient -lavformat -lavcodec -lavutil -lswresample -lpthread -lprotobuf \
-lgrpc++ -lgrpc -lgpr -lupb -labsl_strings -lre2 -laddress_sorting -labsl_time \
-labsl_str_format_internal -labsl_bad_optional_access -labsl_throw_delegate \
-labsl_strings_internal -labsl_int128 -labsl_raw_logging_internal -labsl_time_zone \
-labsl_base -labsl_spinlock_wait -lssl -lcrypto -L../libs

sudo chown root:root main
mv main ../
mv *.o ../build/
cd ..
