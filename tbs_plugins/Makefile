-include $(TARGET).make

$(TARGET)/example.so: LDLIBS = 
$(TARGET)/sma_walsh.so: LDLIBS = -lrt
$(TARGET)/alarm.so: LDLIBS = -lrt

build: 
	$(MAKE) TARGET=x86_64 all
	$(MAKE) TARGET=powerpc all

clean: 
	$(MAKE) TARGET=x86_64 clean-target
	$(MAKE) TARGET=powerpc clean-target

install: 
	$(MAKE) TARGET=x86_64 install-target
	$(MAKE) TARGET=powerpc install-target

all: $(TARGET) $(TARGET)/example.so $(TARGET)/alarm.so $(TARGET)/sma_walsh.so

$(TARGET):
	mkdir -p $(TARGET)

$(TARGET)/%.o: %.c
	$(CROSS_COMPILE)gcc -fPIC -Wall -o $@ -g -c $< -I$(IDIR)/katcp -I$(IDIR)/tcpborphserver3

$(TARGET)/%.so: $(TARGET)/%.o
	$(CROSS_COMPILE)gcc -o $@ $< -shared $(LDLIBS)

clean-target:
	rm -f $(TARGET)/*.o
	rm -f $(TARGET)/*.so
	rmdir -p $(TARGET)

install-target:
	mkdir -p $(INSTALL_DIR)/tbs_plugins
	install -m 644 $(TARGET)/*.so $(INSTALL_DIR)/tbs_plugins
