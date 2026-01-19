all: runme1 runme2 runme3

runme1: main1.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	gcc main1.c xdg-shell-protocol.c -l wayland-client -o runme1

runme2: main2.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	gcc main2.c xdg-shell-protocol.c -l wayland-client -o runme2

runme3: main3.c xdg-shell-client-protocol.h xdg-shell-protocol.c
	gcc main3.c xdg-shell-protocol.c -l wayland-client -o runme3

xdg-shell-client-protocol.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-client-protocol.h

xdg-shell-protocol.c: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
	wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell-protocol.c

.PHONY: clean
clean:
	rm runme1 runme2 runme3 xdg-shell-protocol.c xdg-shell-client-protocol.h