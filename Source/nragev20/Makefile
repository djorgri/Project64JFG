GIT_VERSION := "0.9"
VERSION_BUILD := 0


CXX ?= g++
LD  := $(CXX)
WINDRES ?= windres

OBJECTS := \
	Debug.o \
	DirectInput.o \
	FileAccess.o \
	GBCart.o \
	Interface.o \
	International.o \
	NRagePluginV2.o \
	PakIO.o \
	XInputController.o

RESOURCE := NRagePluginV2.res

TARGET := PJ64_NRage.dll

CPPFLAGS := \
	-DGIT_VERSION=\"$(GIT_VERSION)\" \
	-DVERSION_BUILD=$(VERSION_BUILD) \
	-DNOMINMAX

CXXFLAGS := \
	-shared \
	-fPIC \
	-Os \
	-Wno-write-strings

LIBS := \
	-lcomctl32 \
	-lcomdlg32 \
	-ldinput8 \
	-lgdi32 \
	-lole32 \
	-loleaut32 \
	-luuid

ifeq ($(DYN_WINPTHREAD),1)
LIBS += -lwinpthread
else
LIBS += -static -lwinpthread
endif

LDFLAGS := \
	-mwindows \
	-static-libgcc \
	-static-libstdc++

DEPFILES := $(addprefix .deps/,$(OBJECTS:.o=.d))

.PHONY: all clean

all: .deps Version.h $(TARGET)

-include $(DEPFILES)

clean:
	-rm -f $(TARGET)
	-rm -f $(OBJECTS) $(RESOURCE)
	-rm -f $(DEPFILES)
	-rm -rf .deps

.deps:
	mkdir -p $@


%.res: %.rc
	$(WINDRES) -O coff -o $@ -i $<

%.o: %.cpp
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -MMD -MP -MF .deps/$(@:.o=.d) -o $@ -c $<

$(TARGET): $(OBJECTS) $(RESOURCE)
	$(LD) $(CXXFLAGS) $(LDFLAGS) $^ $(LIBS) -o $@
