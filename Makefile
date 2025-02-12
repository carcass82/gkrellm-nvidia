CFLAGS += -O2 -fpic -Wall -Wextra `pkg-config gkrellm --cflags`
LDFLAGS += -shared
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

install:
	install -m755 $(TARGET) $(DESTDIR)$(INSTALL_DIR)

install-local:
	install -m755 $(TARGET) $(DESTDIR)$(LOCALINSTALL_DIR)

clean:
	rm -rf $(OBJECT) $(TARGET)

# start gkrellm in plugin-test mode
# (needs gkrellm executable in PATH)
test: $(TARGET)
	$(GKRELLM) -p $<
