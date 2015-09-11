######################################################################
# Automatically generated by qmake (2.01a) Thu Jul 25 17:04:04 2013
######################################################################
include(../product.conf)

system(chmod -x *.h)

TEMPLATE = app
TARGET = audioManager

QT +=  core xml

DEPENDPATH += . 
DEPENDPATH += $$PACKAGE_ALSA_DIR/include/alsa
DEPENDPATH += $$PACKAGE_DBUS_DIR/include

INCLUDEPATH += .
INCLUDEPATH += $$PACKAGE_DBUS_DIR/include
INCLUDEPATH += $$PACKAGE_ALSA_DIR/include
INCLUDEPATH += $$PACKAGE_SQLITE_DIR/include
INCLUDEPATH += $$INNOVBOX_PROJECTCONFIG_SRC_DIR
INCLUDEPATH += $$INNOVBOX_WM_INSTALL_DIR/include
INCLUDEPATH += $$INNOVBOX_COMMON_SRC_DIR/message

LIBS += -L$$PACKAGE_SQLITE_DIR/lib -lsqlite3
LIBS += -L$$INNOVBOX_WM_INSTALL_DIR/lib -lwm
LIBS += -L$$PACKAGE_DBUS_DIR/lib -ldbus-1 -lQtDBus
LIBS += -L$$PACKAGE_ALSA_DIR/lib -lasound
LIBS += -L$$PACKAGE_TSLIB_DIR/lib


# Input
HEADERS += audio_main_volume.h i2ccontrol.h
HEADERS += $$INNOVBOX_COMMON_SRC_DIR/message/AudioManageMessage.h
SOURCES += audio_main_volume.cpp i2ccontrol.cpp

QMAKE_POST_LINK =make install

install_include.files = $$INNOVBOX_COMMON_SRC_DIR/message/AudioManageMessage.h

install_include.path = $$INNOVBOX_AUDIOMANAGER_DIR/include

target.path = $$INNOVBOX_AUDIOMANAGER_DIR/bin

INSTALLS += install_include target 

QMAKE_DISTCLEAN += $$install_include.path/* $$target.path/*
