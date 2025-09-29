## 黑方块

《黑方块》是卡济米尔·马列维奇（Kazimir Malevich）的著名画作：

![](https://cdn.nlark.com/yuque/0/2025/png/12732443/1757409429102-92c1ee3a-a368-4ef7-ab34-b4eac96a5d64.png)

在本节的最后，我们将构建一个至少同样酷（而且黑得多）的东西：

![](https://cdn.nlark.com/yuque/0/2025/png/12732443/1757409452230-0c86bb19-7435-4ae8-9d55-631cdde194fd.png)

### 表面（Surfaces）与缓冲区（Buffers）

在 **Wayland 协议** 中，你找不到 “window（窗口）” 这个词；取而代之的是 **“surface（表面）”**。仔细想想这其实很合理：屏幕上的那些矩形东西和墙上的窗户（让你从里面看到外面）有什么共同点呢？不过，我们可以很容易地把 “窗口” 想象成 “表面” —— 就像一张张纸片一样，漂浮在桌面壁纸之上。

但在 Wayland 里，表面并不仅限于桌面窗口。还有其他“漂浮的东西”也是表面：比如菜单等弹出窗口（popup）、鼠标指针（cursor/pointer）、拖拽时的图标（drag’n’drop item）等等。表面的具体身份（比如是窗口、光标还是拖拽图标）由它的 **角色（role）** 决定，这部分我们稍后再讲。但无论角色如何，所有表面都有一些共同的功能：它们都可以显示并更新内容，还可以改变自身的大小。

这里需要理解的一个重点是：**你并不是直接在表面上绘制**。正确的方式是先把内容渲染到一个 **缓冲区（buffer）**，然后把缓冲区附加到表面上。如果你需要更新内容或重新绘制，那么就渲染一张新的图像到另一个缓冲区（有时也可以重用原来的），再把这个新的缓冲区附加到表面上。每一帧画面都需要重复这些步骤。

这种方式相比直接在表面上绘制有几个优势：

+ 你可以提前或并行渲染帧，然后在恰当的时机展示它们。
+ 更重要的是，在 Wayland 中，每一帧都是完整的：**只有在一帧完全渲染完成后，才会将缓冲区附加到表面**。因此不会出现屏幕撕裂（screen tearing）的现象。

要创建一个表面，可以使用 `wl_compositor.create_surface` 请求；要附加缓冲区，可以用 `wl_surface.attach`，然后调用 `wl_surface.commit`：

```c
struct wl_compositor *compositor = ...;
struct wl_surface *surface = wl_compositor_create_surface(compositor);
struct wl_buffer *buffer = ...;
wl_surface_attach(surface, buffer, 0, 0);
wl_surface_commit(surface);
```

这里我们使用了之前从注册表（registry）获取的 **compositor 全局对象**。  
至于如何创建缓冲区，我们马上会讲。但在此之前，需要指出的是：**仅仅创建一个表面并不足以让它显示在屏幕上；必须先给它分配一个角色（role）**。这一点我们稍后会详细解释。

### 分配缓冲区（Allocating a buffer）
Wayland 的设计支持 **不同格式和不同性质的缓冲区（buffer）**。使用核心 Wayland 协议，你只能创建 **共享内存池（shared memory pool）中的缓冲区**，但通过扩展可以增加新的缓冲区类型。例如，`wl_drm` 扩展可以在 GPU 内存中分配缓冲区。

这里我们先使用共享内存池。核心思路是：**不直接通过套接字发送完整的缓冲区内容**（这样既慢又占用资源），而是 **建立一个在客户端和合成器之间共享的内存池**，然后只通过套接字发送缓冲区在池中的位置。

#### 1. 将内存映射到客户端地址空间
在 Linux 下，可以使用 `memfd_create` 系统调用（syscall）创建匿名内存文件；如果不能使用，也可以用临时文件替代：

```c
#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

int size = 200 * 200 * 4;  // 字节数
// 打开匿名文件并写入一些零字节
int fd = syscall(SYS_memfd_create, "buffer", 0);
ftruncate(fd, size);

// 将文件映射到内存
unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
```

#### 2. 通知合成器创建共享内存池
客户端和合成器共享同一个文件，这样合成器也可以通过 `mmap` 访问同一块内存。通过 `wl_shm.create_pool` 请求创建共享内存池：

```c
struct wl_shm *shm = ...;
struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
```

这里，**文件描述符（fd）和大小（size）** 会通过套接字发送给合成器，让它也能映射同样的内存池。

> 注意：共享内存池本身不是缓冲区，你可以在这个池里分配多个缓冲区。
>

#### 3. 在共享内存池中分配缓冲区
分配缓冲区就是告诉合成器：**从共享内存池的某个偏移开始，使用指定的大小和步幅（stride），这个区域表示某种格式的图像**，通过 `wl_shm_pool.create_buffer` 实现：

```c
int width = 200;
int height = 200;
int stride = width * 4;
int size = stride * height;  // 字节数

struct wl_shm_pool *pool = ...;
struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool,
    0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
```

在最简单的情况下，我们分配一个缓冲区占满整个池：偏移量为 0，池的大小就是缓冲区大小。在真实程序中，你可能需要用更复杂的内存分配策略，创建多个缓冲区，而不必每次都映射和取消映射新文件。

#### 4. 完整示例代码
```c
#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_shell *shell;

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
    }
}

void registry_global_remove_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {}

const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // 等待“初始”的全局对象出现
    wl_display_roundtrip(display);

    // 我们需要的对象应该已经就绪！
    if (compositor && shm) {
        printf("Got them all!\n");
    } else {
        printf("Some required globals unavailable\n");
        return 1;
    }

    int width = 200;
    int height = 200;
    int stride = width * 4;
    int size = stride * height;  // 字节数

    // 打开匿名文件并写入零字节
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    ftruncate(fd, size);

    // 映射到内存
    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // 创建共享内存池
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);

    // 在池中分配缓冲区
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool,
        0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    while (1) {
        wl_display_dispatch(display);
    }
}
```

#### 5. 关于渲染
如果现在要渲染内容，需要把像素数据写入 `data` 指针指向的内存中。

但如果只是显示一个黑色方块，其实不必渲染任何内容：因为我们使用的 `XRGB8888` 格式中，四个零字节 `(0,0,0,0)` 就表示一个黑色像素（第一个字节被忽略，其余三个是 RGB 分量），所以缓冲区默认就是 200×200 的黑色像素。

实际上，甚至不需要调用 `mmap`，只要获取文件描述符并传给合成器就行。

### Shell 表面 (Shell Surface)
在上一章中，我们创建了一个 `wl_surface`。然而，如果你现在运行代码，你什么也看不到。这是因为 `wl_surface` 本身只是 Wayland 合成器中的一个抽象对象，它有位置和内容（缓冲区），但它没有“窗口”的属性。它不会显示在你的窗口列表里，你也不能移动它、缩放它，或者给它一个标题栏。

为了让我们的 `surface` (表面) 表现得像一个真正的窗口，我们需要给它一个“角色”(role)。最常见的角色之一就是“shell surface”。

#### Shell 接口
“Shell” 是一个 Wayland 接口，它提供了传统的桌面窗口功能。在旧的版本中，`wl_shell` 是 Wayland 核心协议中定义的一个非常基础和简单的 shell 接口。但是它已经被更强大、更标准的 `xdg-shell` 协议所取代，而且被标记为 deprecated（废弃），新的 compositor（GNOME、KDE、Weston 9+）基本不会再提供它。


使用 `xdg_shell` 的流程如下：

1. **从注册表 (`wl_registry`) 获取全局的 `xdg_wm_base` 对象**

   * 在注册表监听函数里，检查 `interface == "xdg_wm_base"`，然后 `wl_registry_bind`。
   * `xdg_wm_base` 就是 `wl_shell` 的替代，它负责创建 `xdg_surface`。

2. **使用 `xdg_wm_base` 创建 `xdg_surface`**

   * 先用 `wl_compositor_create_surface()` 创建一个 `wl_surface`。
   * 然后调用 `xdg_wm_base_get_xdg_surface(wm_base, wl_surface)` 得到 `xdg_surface`。
   * 这一步相当于 `wl_shell_get_shell_surface()`，只是换成了 `xdg_surface`。

3. **将 `xdg_surface` 提升为顶层窗口 (`xdg_toplevel`)**

   * 使用 `xdg_surface_get_toplevel(xdg_surface)` 得到 `xdg_toplevel`。
   * 这是 `xdg_shell` 里声明“这是一个普通应用窗口”的方式，相当于 `wl_shell_surface_set_toplevel()`。
   * 可以在这里调用 `xdg_toplevel_set_title()` 设置窗口标题。

4. **监听并响应事件**

   * `xdg_wm_base` 会发送 **ping 事件**，你需要回复 `xdg_wm_base_pong()`。
   * `xdg_toplevel` 还会有一些事件，比如 `configure`（调整大小）、`close`（窗口被关闭）。
   * 至少要处理 `xdg_surface.configure`，否则窗口不会显示出来（需要 `ack_configure`）。

注意：

使用 `xdg_shell` 需要 xdg-shell-client-protocol.h 和 xdg-shell-protocol.c 两个文件，需要先用 wayland-protocols 里的 xdg-shell.xml 生成：

```
$ sudo apt install wayland-protocols
$ wayland-scanner client-header \
  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
  xdg-shell-client-protocol.h
$ wayland-scanner private-code \
  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml \
  xdg-shell-protocol.c
```

#### Pings 和 Pongs
Wayland 合成器需要一种方法来确认客户端程序是否仍在正常运行且没有卡死。它通过发送 `ping` 事件来实现这一点。当我们的客户端收到一个 `ping` 事件时，它必须尽快地通过发送一个 `pong` 请求来回应。如果客户端在一定时间内没有回应，合成器就会认为它已无响应，并可能会将其终止。

#### 实现代码
现在，让我们来修改我们的代码。

首先，在 `main.c` 中，我们需要在 `struct client_state` 中添加 `wl_shell` 和 `wl_shell_surface` 对象的指针：

```c
struct client_state {
    /* ... existing state ... */
    struct wl_shell *wl_shell;
    struct wl_shell_surface *wl_shell_surface;
};
```

接下来，我们需要修改 `display_handle_global` 回调函数，以便在遍历全局对象时捕获 `wl_shell` 接口：

```c
static void
display_handle_global(void *data, struct wl_registry *registry,
                      uint32_t name, const char *interface, uint32_t version)
{
    struct client_state *state = data;
    // ... existing wl_compositor and wl_shm handling ...

    if (strcmp(interface, wl_shell_interface.name) == 0) {
        state->wl_shell = wl_registry_bind(registry, name, &wl_shell_interface, 1);
    }
}
```

现在我们有了 `wl_shell` 对象，我们需要为它创建一个监听器来处理 `ping` 事件。在 `main.c` 的顶部添加一个新的监听器和它的回调函数：

```c
static void
wl_shell_surface_ping(void *data, struct wl_shell_surface *shell_surface, uint32_t serial)
{
    wl_shell_surface_pong(shell_surface, serial);
}

static const struct wl_shell_surface_listener wl_shell_surface_listener = {
    .ping = wl_shell_surface_ping,
};
```

这个 `wl_shell_surface_ping` 函数非常简单：当收到 `ping` 事件时，它会立即用 `wl_shell_surface_pong` 回应，并传入相同的 `serial` (序列号)。

最后，我们在 `main` 函数中将所有这些部分组合起来。在创建 `wl_surface` 之后，我们使用它来创建 `wl_shell_surface`：

```c
int
main(int argc, char *argv[])
{
    // ... display, registry, and compositor setup ...

    /* Create a surface */
    state.wl_surface = wl_compositor_create_surface(state.wl_compositor);
    if (state.wl_surface == NULL) {
        fprintf(stderr, "Can't create wl_surface\n");
        return 1;
    }

    /*
     * Here is the new code:
     */
    if (state.wl_shell == NULL) {
        fprintf(stderr, "Didn't find wl_shell\n");
        return 1;
    }
    state.wl_shell_surface = wl_shell_get_shell_surface(state.wl_shell, state.wl_surface);
    if (state.wl_shell_surface == NULL) {
        fprintf(stderr, "Can't create wl_shell_surface\n");
        return 1;
    }
    wl_shell_surface_set_toplevel(state.wl_shell_surface);

    wl_shell_surface_add_listener(state.wl_shell_surface,
            &wl_shell_surface_listener, &state);

    // ... rest of the main function ...
}
```

我们已经成功地让一个窗口出现在屏幕上了。在下一章，我们将学习如何向这个窗口中绘制内容。

### 完整代码（The complete code）
到这里，我们已经具备在屏幕上显示一个黑色方块的所有必要步骤了！下面是完整的代码：

```c
#include <stdio.h>
#include <string.h>

#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>

struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_shell *shell;

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
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name,
            &wl_shell_interface, 1);
    }
}

void registry_global_remove_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {}

const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // 等待“初始”全局对象出现
    wl_display_roundtrip(display);

    struct wl_surface *surface = wl_compositor_create_surface(compositor);
    struct wl_shell_surface *shell_surface = wl_shell_get_shell_surface(shell, surface);
    wl_shell_surface_set_toplevel(shell_surface);

    int width = 200;
    int height = 200;
    int stride = width * 4;
    int size = stride * height;  // 字节数

    // 打开匿名文件并写入零字节
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    ftruncate(fd, size);

    // 映射到内存
    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // 创建共享内存池
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);

    // 在共享内存池中分配缓冲区
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool,
        0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);

    while (1) {
        wl_display_dispatch(display);
    }
}
```

#### 编译与运行
```bash
$ gcc main.c xdg-shell-protocol.c -l wayland-client -o runme
$ ./runme
```

你会看到屏幕上弹出一个黑色方块：

> 注意：它没有窗口边框，所以无法用鼠标拖动。不过你的 Wayland 合成器可能会提供其他方式管理这种不想被管理的窗口，例如 GNOME 可以在按住 Super 键的情况下拖动任意窗口。
>

它的行为应该类似于普通窗口，例如会出现在应用切换器中（GNOME Shell Overview 或按 Alt-Tab 调出的窗口切换器）。

---

#### 注意事项
1. 我们还没有实现关闭窗口功能，也没有响应 ping 事件。
2. 因此，合成器可能在一段时间后认为窗口无响应，并提示你“强制退出”（即 kill 进程）。
3. 当然，你也可以自己用 Ctrl-C 终止进程。
