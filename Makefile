CFLAGS += -O2 -fpic -Wall -Wextra $(shell pkg-config gkrellm --cflags)
LDFLAGS += -shared
INSTALLFLAGS = -m755 -s

SOURCE = nvidia.c
OBJECT = $(SOURCE:.c=.o)
TARGET = $(SOURCE:.c=.so)

GKRELLM = $(shell which gkrellm)
INSTALL_DIR = /usr/lib/gkrellm2/plugins
LOCALINSTALL_DIR = $(HOME)/.gkrellm2/plugins


all: $(TARGET)

$(TARGET): $(OBJECT)
	$(CC) $(LDFLAGS) -o $@ $<

.c.o:
	$(CC) -c $(CFLAGS) -o $@ $<

.PHONY: install install-local clean test

install: $(TARGET)
	install -d $(DESTDIR)$(INSTALL_DIR)
	install $(INSTALLFLAGS) $(TARGET) $(DESTDIR)$(INSTALL_DIR)

install-local: $(TARGET)
	install -d $(DESTDIR)$(LOCALINSTALL_DIR)
	install $(INSTALLFLAGS) $(TARGET) $(DESTDIR)$(LOCALINSTALL_DIR)

clean:
	rm -rf $(OBJECT) $(TARGET)

# start gkrellm in plugin-test mode
# (needs gkrellm executable in PATH)
test: $(TARGET)
	$(GKRELLM) -p $<
