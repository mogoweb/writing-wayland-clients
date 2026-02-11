runme: main.c xdg-shell-client-protocol.h xdg-shell-protocol.c xdg-decoration-client-protocol.h xdg-decoration-protocol.c treeland-foreign-toplevel-manager.h treeland-foreign-toplevel-manager.c
	gcc main.c xdg-shell-protocol.c xdg-decoration-protocol.c treeland-foreign-toplevel-manager.c -l wayland-client -l cairo -o runme

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

xdg-decoration-client-protocol.h: /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	wayland-scanner client-header /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-client-protocol.h

xdg-decoration-protocol.c: /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml
	wayland-scanner private-code /usr/share/wayland-protocols/unstable/xdg-decoration/xdg-decoration-unstable-v1.xml xdg-decoration-protocol.c

treeland-foreign-toplevel-manager.h: /usr/share/treeland-protocols/treeland-foreign-toplevel-manager-v1.xml
	wayland-scanner client-header /usr/share/treeland-protocols/treeland-foreign-toplevel-manager-v1.xml treeland-foreign-toplevel-manager.h

treeland-foreign-toplevel-manager.c: /usr/share/treeland-protocols/treeland-foreign-toplevel-manager-v1.xml
	wayland-scanner private-code /usr/share/treeland-protocols/treeland-foreign-toplevel-manager-v1.xml treeland-foreign-toplevel-manager.c

.PHONY: clean
clean:
	rm runme xdg-shell-client-protocol.h xdg-shell-protocol.c xdg-decoration-client-protocol.h xdg-decoration-protocol.c