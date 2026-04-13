runme: main.c xdg-shell-client-protocol.h xdg-shell-protocol.c viewporter-client-protocol.h viewporter-protocol.c
	gcc main.c xdg-shell-protocol.c viewporter-protocol.c -lwayland-client -o runme

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

viewporter-client-protocol.h: /usr/share/wayland-protocols/stable/viewporter/viewporter.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/viewporter/viewporter.xml viewporter-client-protocol.h

viewporter-protocol.c: /usr/share/wayland-protocols/stable/viewporter/viewporter.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/viewporter/viewporter.xml viewporter-protocol.c

.PHONY: clean
clean:
	rm -f runme xdg-shell-client-protocol.h xdg-shell-protocol.c viewporter-client-protocol.h viewporter-protocol.c
