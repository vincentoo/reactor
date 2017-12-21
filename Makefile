#Author : Simons<liyuling@xunlei.com>
#Date	: 2017-12-21 17:23

COBJS:=$(patsubst %.c,%.o,$(wildcard *.c))
COBJS+=$(patsubst %.cpp,%.o,$(wildcard *.cpp))

TARGET=reactor
.PHONY:all install clean distclean
all: $(TARGET)
$(TARGET):$(COBJS)
	$(CXX) $(CPPFLAGS) $< -o $@ $(LDFLAGS) 

install:
	@$(MV) $(TARGET) $(install_dir)/bin
	make distclean

%.d:%.c
	-@$(CC) -MM $(CFLAGS) $< > $@
%.d:%.cpp
	-@$(CXX) -MM $(CPPFLAGS) $< > $@
	
-include $(COBJS:.o=.d)	

distclean:
	-@$(RM) $(COBJS) *.d $(TARGET)
clean:
	-@$(RM) $(COBJS) $(TARGET)
