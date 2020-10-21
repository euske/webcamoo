# Makefile

CXX=i686-w64-mingw32-c++
RC=i686-w64-mingw32-windres

CFLAGS=-O -Wall -Werror -municode -mwin32
RCFLAGS=-Ocoff
LDFLAGS=-static -mwindows -s
DEFS=-DWINDOWS -DNDEBUG
LIBS=-luser32 -lshell32 -lgdi32 -lole32 -loleaut32 -lstrmiids -lwinpthread
INCLUDES=
TARGET=WebCamoo.exe

all: $(TARGET)

clean:
	-$(RM) $(TARGET)
	-$(RM) *.lib *.exp *.obj *.res *.ilk *.pdb *.manifest

.SUFFIXES: .cpp .obj .exe .rc .res

$(TARGET): WebCamoo.res WebCamoo.obj Filtaa.obj
	$(CXX) $(LDFLAGS) -o$@ $^ $(LIBS)

WebCamoo.cpp: WebCamoo.h
Filtaa.cpp: Filtaa.h WebCamoo.h
WebCamoo.rc: WebCamoo.h
WebCamoo.res: WebCamoo.ico

.cpp.obj:
	$(CXX) $(CFLAGS) -o$@ -c $< $(DEFS) $(INCLUDES)
.rc.res:
	$(RC) $(RCFLAGS) $< $@
