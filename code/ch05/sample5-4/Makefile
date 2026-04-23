# ==========================================
# 跨进程窗口挂载 Demo (纯 Wayland C)
# ==========================================

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lwayland-client -lwayland-cursor

TARGETS = runme

# 协议生成的中间文件
PROTO_C = xdg-shell-protocol.c
PROTO_H = xdg-shell-client-protocol.h

# 系统中的协议 XML 路径 (Debian/Ubuntu/Arch/Fedora 通用)
WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml

.PHONY: all clean protocols

all: protocols $(TARGETS)

# 生成协议文件
protocols: $(PROTO_C) $(PROTO_H)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(XDG_SHELL_XML) $@
xdg-shell-protocol.c:
	wayland-scanner private-code $(XDG_SHELL_XML) $@


# 编译主程序
runme: main.c $(PROTO_C)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	@echo "  CLEAN"
	@rm -f $(TARGETS) $(PROTO_C) $(PROTO_H)
