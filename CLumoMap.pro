QT += network
QT += core widgets gui
QT += concurrent

# install
 INSTALLS += widget

HEADERS += \
    CCloudPoints.h \
    CLumoMap.h \
    CComm.h \
    CMainWin.h

SOURCES += \
           CLumoMap.cpp \
           CComm.cpp \
           CMainWin.cpp \
           main.cpp
FORMS +=
QMAKE_CXXFLAGS += /utf-8
CONFIG += c++11
