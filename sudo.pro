TEMPLATE = app
TARGET = sudo
INCLUDEPATH += . \
    include \
    src \
    /usr/include \
    $$system(python-config --includes | sed -e 's,-I,,g') \

HEADERS += $$system(find . -name "\\*.h")

LEXSOURCES += plugins/sudoers/toke.l
YACCSOURCES += plugins/sudoers/getdate.y plugins/sudoers/gram.y

SOURCES += $$system(find . -name '\\*.c') \

OTHER_FILES += $$system(find . -name '\\*.inc') \
               $$system(find . -name '\\*.in') \

PRECOMPILED_HEADER += config.h

DEFINES += LOCALEDIR=\\\"/tmp\\\"
