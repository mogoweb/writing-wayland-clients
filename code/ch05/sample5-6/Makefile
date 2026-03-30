# ==========================================
# xdg_activation_v1 Demo
# ==========================================

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lwayland-client

TARGET = runme

# 协议生成的中间文件
PROTO_C = xdg-shell-protocol.c xdg-activation-v1-protocol.c
PROTO_H = xdg-shell-client-protocol.h xdg-activation-v1-client-protocol.h

# 系统中的协议 XML 路径
WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDG_ACTIVATION_XML = $(WAYLAND_PROTOCOLS_DIR)/staging/xdg-activation/xdg-activation-v1.xml

.PHONY: all clean protocols

all: protocols $(TARGET)

# 生成协议文件
protocols: $(PROTO_C) $(PROTO_H)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(XDG_SHELL_XML) $@

xdg-shell-protocol.c:
	wayland-scanner private-code $(XDG_SHELL_XML) $@

xdg-activation-v1-client-protocol.h:
	wayland-scanner client-header $(XDG_ACTIVATION_XML) $@

xdg-activation-v1-protocol.c:
	wayland-scanner private-code $(XDG_ACTIVATION_XML) $@

# 编译主程序
$(TARGET): main.c $(PROTO_C)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	@echo "  CLEAN"
	@rm -f $(TARGET) $(PROTO_C) $(PROTO_H)
