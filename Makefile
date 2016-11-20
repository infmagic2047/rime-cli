include config.mk

CPPFLAGS += -DPROJECT_NAME=\"$(PROJECT_NAME)\" -DPROJECT_VERSION=\"$(PROJECT_VERSION)\" -DRIME_SHARED_DATA_DIR=\"$(RIME_SHARED_DATA_DIR)\"
CFLAGS += $(shell pkg-config --cflags rime) $(shell pkg-config --cflags json-c)
LDLIBS = $(shell pkg-config --libs rime) $(shell pkg-config --libs json-c)

all: rime-cli

clean:
	rm -f rime-cli

.PHONY: all clean
