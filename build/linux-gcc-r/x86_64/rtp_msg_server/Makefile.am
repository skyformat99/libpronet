probindir = ${prefix}/libpronet/bin
prolibdir = ${prefix}/libpronet/lib

#############################################################################

probin_PROGRAMS = rtp_msg_server

rtp_msg_server_SOURCES = ../../../../src/pro/rtp_msg_server/main.cpp          \
                         ../../../../src/pro/rtp_msg_server/msg_db.cpp        \
                         ../../../../src/pro/rtp_msg_server/msg_server.cpp    \
                         ../../../../src/pro/rtp_msg_server/db_connection.cpp \
                         ../../../../src/pro/rtp_msg_server/sqlite3.c

rtp_msg_server_CPPFLAGS = -DSQLITE_THREADSAFE=1

rtp_msg_server_CFLAGS   = -fno-strict-aliasing
rtp_msg_server_CXXFLAGS = -fno-strict-aliasing

rtp_msg_server_LDFLAGS = -Wl,-rpath,.:../lib:${prolibdir} -Wl,--no-undefined
rtp_msg_server_LDADD   =

LIBS = ../pro_rtp/libpro_rtp.so       \
       ../pro_net/libpro_net.so       \
       ../pro_util/libpro_util.a      \
       ../pro_shared/libpro_shared.so \
       ../mbedtls/libmbedtls.a        \
       -lstdc++                       \
       -ldl                           \
       -lrt                           \
       -lpthread                      \
       -lm                            \
       -lgcc                          \
       -lc
