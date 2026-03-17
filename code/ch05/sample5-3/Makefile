# ==========================================
# 跨进程窗口挂载 Demo (纯 Wayland C)
# ==========================================

CC = gcc
CFLAGS = -Wall -O2
LDFLAGS = -lwayland-client

TARGETS = wayland_parent wayland_child

# 协议生成的中间文件
PROTO_C = xdg-shell-protocol.c xdg-foreign-unstable-v2-protocol.c xdg-decoration-unstable-v1-protocol.c
PROTO_H = xdg-shell-client-protocol.h xdg-foreign-unstable-v2-client-protocol.h xdg-decoration-unstable-v1-client-protocol.h

# 系统中的协议 XML 路径 (Debian/Ubuntu/Arch/Fedora 通用)
WAYLAND_PROTOCOLS_DIR = /usr/share/wayland-protocols
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS_DIR)/stable/xdg-shell/xdg-shell.xml
XDG_FOREIGN_XML = $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-foreign/xdg-foreign-unstable-v2.xml
XDG_DECO_XML = $(WAYLAND_PROTOCOLS_DIR)/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml

.PHONY: all clean protocols

all: protocols $(TARGETS)

# 生成协议文件
protocols: $(PROTO_C) $(PROTO_H)

xdg-shell-client-protocol.h:
	wayland-scanner client-header $(XDG_SHELL_XML) $@
xdg-shell-protocol.c:
	wayland-scanner private-code $(XDG_SHELL_XML) $@

xdg-foreign-unstable-v2-client-protocol.h:
	wayland-scanner client-header $(XDG_FOREIGN_XML) $@
xdg-foreign-unstable-v2-protocol.c:
	wayland-scanner private-code $(XDG_FOREIGN_XML) $@

xdg-decoration-unstable-v1-client-protocol.h:
	wayland-scanner client-header $(XDG_DECO_XML) $@
xdg-decoration-unstable-v1-protocol.c:
	wayland-scanner private-code $(XDG_DECO_XML) $@

# 编译主程序
wayland_parent: wayland_parent.c $(PROTO_C)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

wayland_child: wayland_child.c $(PROTO_C)
	@echo "  CC      $@"
	@$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	@echo "  CLEAN"
	@rm -f $(TARGETS) $(PROTO_C) $(PROTO_H)
