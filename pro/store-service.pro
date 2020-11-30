QT -= gui

CONFIG -= c++11 console
CONFIG += app_bundle

# The following define makes your compiler emit warnings if you use
# any Qt feature that has been marked deprecated (the exact warnings
# depend on your compiler). Please consult the documentation of the
# deprecated API in order to know how to port your code away from it.
DEFINES += QT_DEPRECATED_WARNINGS

# You can also make your code fail to compile if it uses deprecated APIs.
# In order to do so, uncomment the following line.
# You can also select to disable deprecated APIs only up to a certain version of Qt.
#DEFINES += QT_DISABLE_DEPRECATED_BEFORE=0x060000    # disables all the APIs deprecated before Qt 6.0.0

QMAKE_LFLAGS += '-Wl,-Map,a.map'

SOURCES += \
    ../src/backup.cpp \
    ../src/cJSON.cpp \
    ../src/common.cpp \
    ../src/grpc/gb28181.grpc.pb.cc \
    ../src/grpc/gb28181.pb.cc \
    ../src/grpc/grpc_client.cc \
    ../src/http.cpp \
    ../src/log.cpp \
    ../src/main.cpp \
    ../src/media.cpp \
    ../src/monitor.cpp \
    ../src/mysql.cpp \
    ../src/rtmpsrv.cpp \
    ../src/server.cpp \
    ../src/store.cpp \
    ../src/stream.cpp \
    ../src/thread.cpp

LIBS += -L../libs \
    -lavformat \
    -lavcodec \
    -lavutil \
    -lswresample \
    -lpthread \
    -lmysqlclient \
    -lrtmp \
    -lprotobuf \
    -lgrpc++ -lgrpc -lgpr -lupb -labsl_strings -lre2 -laddress_sorting -labsl_time \
    -labsl_str_format_internal -labsl_bad_optional_access -labsl_throw_delegate \
    -labsl_strings_internal -labsl_int128 -labsl_raw_logging_internal -labsl_time_zone \
    -labsl_base -labsl_spinlock_wait -lssl -lcrypto

INCLUDEPATH += ../inc \
               ../inc/grpc

# Default rules for deployment.
qnx: target.path = /tmp/$${TARGET}/bin
else: unix:!android: target.path = /opt/$${TARGET}/bin
!isEmpty(target.path): INSTALLS += target

HEADERS += \
    ../src/backup.h \
    ../src/cJSON.h \
    ../src/common.h \
    ../src/grpc/gb28181.grpc.pb.h \
    ../src/grpc/gb28181.pb.h \
    ../src/http.h \ \
    ../src/log.h \
    ../src/monitor.h

DISTFILES += \
    ../src/grpc/gb28181.proto \
    ../src/grpc/gen.sh
