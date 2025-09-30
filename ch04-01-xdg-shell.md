### xdg-shell

xdg-shell 是一个 Wayland 协议扩展，目的是替代核心 Wayland 的 `wl_shell`。引用 Wayland 开发者之一 Jasper St. Pierre 的话：“xdg-shell 是 `wl_shell` 的直接替代品。`wl_shell_surface` 有许多令人沮丧的限制，并且由于它被包含在 Wayland 1.0 的核心协议中，因此更难以修改和改进。”

这里有几点需要理解：

首先，xdg-shell 是一个协议扩展，并不是 Wayland 核心协议的一部分。因此，我们一直在使用的 wayland-client 库并不直接支持它。不过，由于 Wayland 从一开始就被设计为可扩展的，所以我们可以（而且相当容易）自动生成所有额外的代码，使我们的程序能够像使用核心 Wayland 一样使用 xdg-shell。

其次，xdg-shell 目前已经是稳定扩展。

Wayland 内置了一个机制，让客户端和服务器协商它们支持的接口版本（在 wl_registry.global 事件和 wl_registry.bind 请求中的 version 参数），但这只适用于向后兼容的修改。

要查看你的合成器支持的 xdg-shell 版本（如果支持的话），可以列出注册表中公布的所有全局对象：如果使用 weston 合成器，可以运行 `weston-info`，要么用你自己的程序（比如我们在上一节写的那个程序）。

你可以在 freedesktop 的 wayland-protocols 仓库中找到 xdg-shell 的 XML 协议定义。具体来说，这里有 `xdg-shell.xml`。你可以手动下载该文件，或者使用系统本地已有的副本（如果存在的话，比如我的在 `/usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml`）。接着，使用 `wayland-scanner` 工具生成对应的 `.h` 和 `.c` 文件：

```
$ wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h
$ wayland-scanner code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c
```

在 `main.c` 中，将生成的 `xdg-shell.h` 头文件与 `wayland-client.h` 一起包含进去：

```
#include <wayland-client.h>
#include "xdg-shell.h"
```

并将其一起编译：

```
$ gcc main.c xdg-shell.c -l wayland-client -o runme
```

它应该能够无错误地编译，并显示和之前一样的黑色方块（因为我们还没有做任何改动）。

这开始变得有些重复了，所以我们来写一个 Makefile。把下面的内容放到 Makefile 中：

```
runme: main.c xdg-shell.h xdg-shell.c
        gcc main.c xdg-shell.c -l wayland-client -o runme

xdg-shell.h: /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml
        wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h

xdg-shell.c: xdg-shell.xml
        wayland-scanner code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c

.PHONY: clean
clean:
        rm runme xdg-shell.c xdg-shell.h
```

现在，你只需要用下面这条命令就可以编译并运行它：

```
$ make
$ ./runme
```

让我们开始真正使用 xdg-shell。首先，把全局的 `xdg_shell` 接口绑定上：

```
struct wl_compositor *compositor;
struct wl_shm *shm;
struct xdg_shell *xdg_shell;

void registry_global_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
) {
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, name,
            &wl_compositor_interface, 3);
    } else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, name,
            &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_shell") == 0) {
        xdg_shell = wl_registry_bind(registry, name,
            &xdg_shell_interface, 1);
    }
}
```

接下来，调用 `xdg-shell` ：

```
struct wl_surface *surface = wl_compositor_create_surface(compositor);
struct xdg_surface *xdg_surface =
    xdg_shell_get_xdg_surface(xdg_shell, surface);
struct xdg_toplevel *xdg_toplevel =
    xdg_surface_get_toplevel(xdg_surface);
```

与 wl_shell_surface 不同，在那里一个 surface 先被赋予 shell surface 的角色，然后再额外设置为顶层窗口；在 xdg-shell 中，xdg_surface 本身并不是一个角色，而 xdg_toplevel 才是。
如果从继承的角度来理解，可以认为我们有 wl_surface → xdg_surface → xdg_toplevel。

如果你现在编译并运行这个程序，就会遇到一个错误：

```
$ make
$ ./runme
wl_surface@3: error 3: buffer committed to unconfigured xdg_surface
```

这是因为 xdg-shell 要求在提交缓冲区到对应的 wl_surface 之前，必须先让 xdg_surface 完成配置。我们需要处理 xdg_toplevel.configure 事件，它本质上是合成器告诉我们应该以什么大小绘制表面（这只是一个提示，我们完全可以忽略），以及该表面当前是否处于最大化、全屏、活动等状态（以便我们决定如何绘制窗口装饰）。我们还需要处理 xdg_shell.configure 事件，这是一个巧妙的机制，用来让所有操作具备原子性并避免竞争条件。

```
void xdg_toplevel_configure_handler
(
    void *data,
    struct zxdg_toplevel_v6 *xdg_toplevel,
    int32_t width,
    int32_t height,
    struct wl_array *states
) {
    printf("configure: %dx%d\n", width, height);
}

void xdg_toplevel_close_handler
(
    void *data,
    struct zxdg_toplevel_v6 *xdg_toplevel
) {
    printf("close\n");
}

const struct zxdg_toplevel_v6_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure_handler,
    .close = xdg_toplevel_close_handler
};

void xdg_surface_configure_handler
(
    void *data,
    struct zxdg_surface_v6 *xdg_surface,
    uint32_t serial
) {
    zxdg_surface_v6_ack_configure(xdg_surface, serial);
}

const struct zxdg_surface_v6_listener xdg_surface_listener = {
    .configure = xdg_surface_configure_handler
};
```

在这个简单的示例中，我们忽略了 xdg_toplevel.configure 给我们的所有提示，但会在接收到 xdg_surface.configure 时使用 xdg_surface.ack_configure 正确地进行确认。我们还编写了一个 xdg_toplevel.close 的空处理函数（stub handler），因为在添加监听器时，wayland-client 要求你必须为接口可能发出的所有事件提供处理函数。稍后我们会实现真正的关闭和退出逻辑。

接下来，在 main() 中：

```
struct wl_surface *surface = wl_compositor_create_surface(compositor);
struct zxdg_surface_v6 *xdg_surface =
    zxdg_shell_v6_get_xdg_surface(xdg_shell, surface);
zxdg_surface_v6_add_listener(xdg_surface, &xdg_surface_listener, NULL);
struct zxdg_toplevel_v6 *xdg_toplevel =
    zxdg_surface_v6_get_toplevel(xdg_surface);
zxdg_toplevel_v6_add_listener(xdg_toplevel, &xdg_toplevel_listener, NULL);

// signal that the surface is ready to be configured
wl_surface_commit(surface);

// ...
// create a pool
// create a buffer
// ...

// wait for the surface to be configured
wl_display_roundtrip(display);

wl_surface_attach(surface, buffer, 0, 0);
wl_surface_commit(surface);

while (1) {
    wl_display_dispatch(display);
}
```

除了设置监听器之外，我们还做了两个重要的改动。首先，我们会在尚未附加任何缓冲区之前，立即调用 wl_surface.commit。这会促使合成器（compositor）发出相应的 configure 事件。在附加缓冲区之前，我们会通过额外调用 wl_display_roundtrip() 来等待这些事件被接收并处理。

这样应该就足够成功显示那个黑色方块了。不过，既然已经做到这一步，我们也顺便实现一下对合成器 ping 的响应，以免合成器认为我们的程序没有响应：

```
void xdg_shell_ping_handler
(
    void *data,
    struct zxdg_shell_v6 *xdg_shell,
    uint32_t serial
) {
    zxdg_shell_v6_pong(xdg_shell, serial);
    printf("ping-pong\n");
}

const struct zxdg_shell_v6_listener xdg_shell_listener = {
    .ping = xdg_shell_ping_handler
};
```

并在 main() 中添加：

```
// wait for the "initial" set of globals to appear
wl_display_roundtrip(display);

zxdg_shell_v6_add_listener(xdg_shell, &xdg_shell_listener, NULL);
```

编译并运行之：

```
$ make
$ ./runme
configure: 0x0
configure: 0x0
ping-pong
^C
```

这一次我们又成功显示出了黑色方块，不过是通过 xdg-shell！我们可以看到，合成器并没有给出任何有用的尺寸提示（但如果你尝试让窗口最大化贴靠时，它就会提供）。此外，它有时还会向我们的程序发送 ping（在 GNOME Shell 中，这通常发生在窗口在概览模式下被选中时）。
