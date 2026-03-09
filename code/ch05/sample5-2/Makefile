runme: main.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	gcc main.c xdg-shell-protocol.c -l wayland-client -l cairo -o runme

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

.PHONY: clean
clean:
	rm runme xdg-shell-client-protocol.h xdg-shell-protocol.c