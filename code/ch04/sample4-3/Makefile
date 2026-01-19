runme: main.c xdg-shell.h xdg-shell.c
	gcc main.c xdg-shell.c -l wayland-client -l wayland-cursor -o runme

xdg-shell.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h

xdg-shell.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c

.PHONY: clean
clean:
	rm runme xdg-shell.c xdg-shell.h