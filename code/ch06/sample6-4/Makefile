all: client_a client_b

client_a: client_a.c xdg-shell-client-protocol.h xdg-shell-protocol.c xdg-decoration-client-protocol.h xdg-decoration-protocol.c xx-zones-client-protocol.h xx-zones-protocol.c
	gcc client_a.c xdg-shell-protocol.c xdg-decoration-protocol.c xx-zones-protocol.c -l wayland-client -l cairo -o client_a

client_b: client_b.c xdg-shell-client-protocol.h xdg-shell-protocol.c xdg-decoration-client-protocol.h xdg-decoration-protocol.c xx-zones-client-protocol.h xx-zones-protocol.c
	gcc client_b.c xdg-shell-protocol.c xdg-decoration-protocol.c xx-zones-protocol.c -l wayland-client -l cairo -o client_b

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

xdg-decoration-client-protocol.h: /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	wayland-scanner client-header /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h

xdg-decoration-protocol.c: /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	wayland-scanner private-code /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c

xx-zones-client-protocol.h: xx-zones-v1.xml
	wayland-scanner client-header xx-zones-v1.xml xx-zones-client-protocol.h

xx-zones-protocol.c: xx-zones-v1.xml
	wayland-scanner private-code xx-zones-v1.xml xx-zones-protocol.c

.PHONY: clean
clean:
	rm client_a client_b xdg-shell-client-protocol.h xdg-shell-protocol.c xdg-decoration-client-protocol.h xdg-decoration-protocol.c