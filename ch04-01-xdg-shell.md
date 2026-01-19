## xdg-shell

**注**：由于《Writing Wayland Clients》教程中 xdg-shell 章节基于尚未稳定的 `xdg-shell-unstable-v6.xml` 协议，而该协议现已被稳定版 `xdg-shell.xml` 取代，原文内容已不再完全适用。因此，本部分内容改自《The Wayland Protocol》的 *XDG Shell Basics* 章节。

在 Wayland 架构中，wl_surface 只是一个像素缓冲的承载体，它本身并不具备“窗口”的语义：没有标题栏、没有最大化、也没有“这是一个主窗口还是菜单”的概念。

xdg-shell 正是为了解决这个问题而诞生的。

XDG（跨桌面组织，cross-desktop group）shell 是 Wayland 的一个标准协议扩展，用于描述应用程序窗口的语义。

xdg-shell 旨在用来替代 Wayland 核心协议中的 wl_shell。正如 Wayland 开发者之一 Jasper St. Pierre 所说：“xdg-shell 是 wl_shell 的直接替代品。wl_shell_surface 存在一些令人沮丧的限制，而且由于它被纳入了 Wayland 1.0 核心协议，使得对它进行修改和改进变得更加困难。”

因此，所有现代 Wayland 桌面环境（GNOME、KDE、wlroots 系）都已经以 xdg-shell 作为事实标准。

这里有几点需要理解。

首先，xdg-shell 是一个协议扩展，并不是 Wayland 核心协议的一部分。因此，我们一直使用的 wayland-client 库并没有为它提供直接支持。不过，由于 Wayland 从设计之初就具备良好的可扩展性，可以（而且相当容易地）自动生成所需的附加代码，使我们的程序能够像使用核心 Wayland 协议一样使用 xdg-shell。

其次，xdg-shell 目前已经是一个稳定的扩展。Wayland 内置了一种机制，用于让客户端和服务器协商它们各自支持哪些接口以及对应的版本（即 wl_registry.global 事件和 wl_registry.bind 请求中的 version 参数）。

你可以在 freedesktop 的 wayland-protocols 仓库中找到 xdg-shell 的 XML 协议定义文件，对应的文件是 xdg-shell.xml。你可以手动下载该文件，或者使用系统中已有的本地副本（例如我这里位于 /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml）。接下来，使用 wayland-scanner 工具生成相应的 .h 和 .c 文件：

```bash
$ wayland-scanner client-header /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.h
$ wayland-scanner private-code /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml xdg-shell.c
```

xdg-shell 定义了两种 `wl_surface` 的角色：

* **“toplevel”**：表示应用程序的顶级窗口；
* **“popup”**：用于上下文菜单、下拉菜单、工具提示等，这些通常是顶级窗口的子窗口。

通过这两种角色，你可以构建一个由多个 surface 组成的层级树结构：顶级窗口位于树的根部，而弹出窗口或额外的顶级窗口位于叶子节点。

该协议还定义了一个 **positioner（定位器）接口**，用于在仅拥有有限的窗口周边信息的情况下，帮助确定弹出窗口（popup）的显示位置。

### xdg_wm_base

`xdg_wm_base` 是本协议中定义的唯一全局接口，它提供了创建所需的其他对象的请求。最基本的实现从处理 "ping" 事件开始，当合成器发送此事件时，您应及时以 "pong" 请求进行响应，以表明您的程序尚未陷入死锁。另一个请求处理定位器的创建，即前面提到的对象。

### XDG Surfaces

在 `xdg-shell` 协议中的 surfaces 被称为 xdg_surface。此接口提供了顶层窗口和弹出窗口这两种 XDG Surface 所共有的一小部分通用功能。然而，每种 XDG Surface 的语义差异仍然很大，因此必须通过赋予其额外的角色来明确指定。

`xdg_surface` 接口提供了额外的请求，用于分配更具体的弹出窗口和顶层窗口角色。一旦我们将对象绑定到 xdg_wm_base 全局对象上，就可以使用 get_xdg_surface 请求来为某个 wl_surface 获取一个 xdg_surface。

```xml
<request name="get_xdg_surface">
  <arg name="id" type="new_id" interface="xdg_surface"/>
  <arg name="surface" type="object" interface="wl_surface"/>
</request>
```
`xdg_surface` 接口除了提供为 Surface 分配更具体的“顶层窗口”或“弹出窗口”角色的请求外，还包含一些两种角色共有的重要功能。在继续探讨针对顶层窗口和弹出窗口的特定语义之前，让我们先来回顾这些通用功能。

```xml
<event name="configure">
  <arg name="serial" type="uint" summary="serial of the configure event"/>
</event>

<request name="ack_configure">
  <arg name="serial" type="uint" summary="the serial from the configure event"/>
</request>
```

xdg-surface 最重要的 API 是这对组合：configure​ 和 ack_configure。Wayland 的一个目标是实现完美的每一帧显示，这意味着不会显示任何处于半完成状态变更的帧。而为了实现这一点，我们必须在客户端和服务器之间同步这些变更。对于 XDG Surface，这对消息正是支持此机制的途径。

我们目前只介绍基础概念，因此可以这样总结这两个事件的重要性：当来自服务器的事件通知您某个 Surface 的配置（或重新配置）时，请将它们应用到待定状态。当 configure 事件到达时，应用这些待定变更，使用 ack_configure 来确认您已完成操作，然后渲染并提交新的一帧。

```
<request name="set_window_geometry">
  <arg name="x" type="int"/>
  <arg name="y" type="int"/>
  <arg name="width" type="int"/>
  <arg name="height" type="int"/>
</request>
```
set_window_geometry​ 请求主要供使用客户端装饰的应用程序使用，用以区分其表面中哪些部分被视为窗口的一部分，哪些部分不是。通常，这用于将窗口背后渲染的客户端投影阴影排除在窗口范围之外。合成器可能会运用此信息来管理其自身排列窗口以及与窗口交互的行为。

### 应用程序窗口

经过诸多准备，我们现在终于要使用 XDG顶层窗口（XDG toplevel）​ 接口来显示应用程序窗口了。该接口包含了管理应用程序窗口所需的众多请求和事件，例如处理最小化、最大化等状态，以及设置窗口标题等。我们将在后续章节详细探讨它的每个部分，现在先关注基础内容。

根据上一节 XDG Surfaces 的介绍，我们已知能从 wl_surface获取一个 xdg_surface，但这仅是第一步：使 surface 纳入 XDG shell 的体系。下一步是将这个 xdg_surface转变为 XDG顶层窗口​: 即一个"顶层"应用程序窗口。之所以称为"顶层"，是因为在最终由 XDG shell 创建的窗口和弹出菜单层级中，它处于最高级别。要创建这样一个顶层窗口，我们可以使用 xdg_surface接口中的相应请求：

```xml
<request name="get_toplevel">
  <arg name="id" type="new_id" interface="xdg_toplevel"/>
</request>
```
这个新的 xdg_toplevel接口为我们提供了许多用于管理应用程序窗口生命周期的请求和事件。如果您遵循以下步骤：处理好前一节讨论的 XDG surface 的 configure和 ack_configure配置，并将一个 wl_buffer附加（attach）到我们的 wl_surface并提交（commit），那么一个应用程序窗口就会出现，并将您缓冲区的内容呈现给用户。

```xml
<request name="set_title">
  <arg name="title" type="string"/>
</request>
```

这个请求的含义应该比较直观，另外还有一个类似的请求：

```
<request name="set_app_id">
  <arg name="app_id" type="string"/>
</request>
```
标题通常显示在窗口装饰、任务栏等位置，而应用ID则用于标识您的应用程序或将窗口分组管理。例如，您可以将窗口标题设为"应用程序窗口 —— The Wayland Protocol —— Firefox"，并将应用ID设为"firefox"。

综上所述，以下步骤将引导您从零开始显示一个窗口：
1. 绑定到 wl_compositor并使用它创建一个 wl_surface
2. 绑定到 xdg_wm_base并利用您的 wl_surface创建一个 xdg_surface
3. 通过 xdg_surface.get_toplevel从该 xdg_surface创建一个 xdg_toplevel
4. 为 xdg_surface配置监听器并等待配置事件
5. 绑定到您选择的缓冲区分配机制（例如 wl_shm）并分配一个共享缓冲区，然后将内容渲染到其中
6. 使用 wl_surface.attach将 wl_buffer附加到 wl_surface
7. 使用 xdg_surface.ack_configure并传入配置事件中的序列号，确认您已准备好合适的帧
8. 发送 wl_surface.commit请求

下面给一个具体的示例。

### 最小示例

```c
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include "xdg-shell.h"

/* Shared memory support code */
static void
randname(char *buf)
{
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    long r = ts.tv_nsec;
    for (int i = 0; i < 6; ++i) {
        buf[i] = 'A'+(r&15)+(r&16)*2;
        r >>= 5;
    }
}

static int
create_shm_file(void)
{
    int retries = 100;
    do {
        char name[] = "/wl_shm-XXXXXX";
        randname(name + sizeof(name) - 7);
        --retries;
        int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0) {
            shm_unlink(name);
            return fd;
        }
    } while (retries > 0 && errno == EEXIST);
    return -1;
}

static int
allocate_shm_file(size_t size)
{
    int fd = create_shm_file();
    if (fd < 0)
        return -1;
    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Wayland code */
struct client_state {
    /* Globals */
    struct wl_display *wl_display;
    struct wl_registry *wl_registry;
    struct wl_shm *wl_shm;
    struct wl_compositor *wl_compositor;
    struct xdg_wm_base *xdg_wm_base;
    /* Objects */
    struct wl_surface *wl_surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
};

static void
wl_buffer_release(void *data, struct wl_buffer *wl_buffer)
{
    /* Sent by the compositor when it's no longer using this buffer */
    wl_buffer_destroy(wl_buffer);
}

static const struct wl_buffer_listener wl_buffer_listener = {
    .release = wl_buffer_release,
};

static struct wl_buffer *
draw_frame(struct client_state *state)
{
    const int width = 640, height = 480;
    int stride = width * 4;
    int size = stride * height;

    int fd = allocate_shm_file(size);
    if (fd == -1) {
        return NULL;
    }

    uint32_t *data = mmap(NULL, size,
            PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        close(fd);
        return NULL;
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(state->wl_shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0,
            width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);

    /* Draw checkerboxed background */
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            if ((x + y / 8 * 8) % 16 < 8)
                data[y * width + x] = 0xFF666666;
            else
                data[y * width + x] = 0xFFEEEEEE;
        }
    }

    munmap(data, size);
    wl_buffer_add_listener(buffer, &wl_buffer_listener, NULL);
    return buffer;
}

static void
xdg_surface_configure(void *data,
        struct xdg_surface *xdg_surface, uint32_t serial)
{
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    struct wl_buffer *buffer = draw_frame(state);
    wl_surface_attach(state->wl_surface, buffer, 0, 0);
    wl_surface_commit(state->wl_surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void
registry_global(void *data, struct wl_registry *wl_registry,
        uint32_t name, const char *interface, uint32_t version)
{
    struct client_state *state = data;
    if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->wl_shm = wl_registry_bind(
                wl_registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->wl_compositor = wl_registry_bind(
                wl_registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(
                wl_registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base,
                &xdg_wm_base_listener, state);
    }
}

static void
registry_global_remove(void *data,
        struct wl_registry *wl_registry, uint32_t name)
{
    /* This space deliberately left blank */
}

static const struct wl_registry_listener wl_registry_listener = {
    .global = registry_global,
    .global_remove = registry_global_remove,
};

int
main(int argc, char *argv[])
{
    struct client_state state = { 0 };
    state.wl_display = wl_display_connect(NULL);
    state.wl_registry = wl_display_get_registry(state.wl_display);
    wl_registry_add_listener(state.wl_registry, &wl_registry_listener, &state);
    wl_display_roundtrip(state.wl_display);

    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(
            state.xdg_wm_base, state.wl_surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "Example client");
    wl_surface_commit(state.wl_surface);

    while (wl_display_dispatch(state.wl_display)) {
        /* This space deliberately left blank */
    }

    return 0;
}
```