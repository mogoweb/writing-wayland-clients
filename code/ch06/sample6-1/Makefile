runme: main.c xdg-shell-client-protocol.h xdg-shell-protocol.c treeland-dde-shell-client-protocol.h treeland-dde-shell-protocol.c
	gcc main.c xdg-shell-protocol.c treeland-dde-shell-protocol.c -l wayland-client -l cairo -o runme

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

treeland-dde-shell-client-protocol.h: /usr/share/treeland-protocols/treeland-dde-shell-v1.xml
	wayland-scanner client-header /usr/share/treeland-protocols/treeland-dde-shell-v1.xml treeland-dde-shell-client-protocol.h

treeland-dde-shell-protocol.c: /usr/share/treeland-protocols/treeland-dde-shell-v1.xml
	wayland-scanner private-code /usr/share/treeland-protocols/treeland-dde-shell-v1.xml treeland-dde-shell-protocol.c

.PHONY: clean
clean:
	rm runme xdg-shell-client-protocol.h xdg-shell-protocol.c treeland-dde-shell-client-protocol.h treeland-dde-shell-protocol.c