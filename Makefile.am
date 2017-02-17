ACLOCAL_AMFLAGS=-I m4
SUBDIRS = inc
lib_LTLIBRARIES=libopx_nas_ndi.la

libopx_nas_ndi_la_SOURCES= src/hal_shell.c src/nas_ndi_init.c src/nas_ndi_utils.cpp \
           src/nas_ndi_port.c src/nas_ndi_packet.c src/nas_ndi_route.c \
           src/nas_ndi_router_interface.c src/nas_ndi_vlan.c \
           src/nas_ndi_acl.cpp src/nas_ndi_acl_utl.cpp \
           src/nas_ndi_hash.c src/nas_ndi_qos_policer.cpp src/nas_ndi_qos_port.cpp \
           src/nas_ndi_qos_queue.cpp src/nas_ndi_qos_wred.cpp \
           src/nas_ndi_qos_map.cpp src/nas_ndi_qos_scheduler.cpp \
           src/nas_ndi_qos_scheduler_group.cpp src/nas_ndi_mirror.cpp \
           src/nas_ndi_sflow.cpp src/nas_ndi_stg.cpp src/nas_ndi_lag.c \
           src/nas_ndi_mac.c src/nas_ndi_port_map.cpp src/nas_ndi_mac_utl.cpp \
           src/nas_ndi_switch.cpp src/nas_ndi_port_utils.cpp \
           src/nas_ndi_qos_buffer_pool.cpp src/nas_ndi_qos_buffer_profile.cpp \
           src/nas_ndi_qos_priority_group.cpp \
           src/nas_ndi_plat_stat.c

libopx_nas_ndi_la_CPPFLAGS= -D_FILE_OFFSET_BITS=64 -I$(top_srcdir)/inc/opx -I$(includedir)/opx

libopx_nas_ndi_la_CXXFLAGS=-std=c++11

libopx_nas_ndi_la_LDFLAGS=-shared -version-info 1:1:0

libopx_nas_ndi_la_LIBADD=-lpthread -lopx_common -lopx_logging -lopx_sai_common -lopx_nas_common -lopx_cps_class_map
