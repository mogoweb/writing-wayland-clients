## 编写一个基础本应用

### 设置环境
虽然有一些独立的 Wayland 实现，比如 **Skylane** 和 **Sudbury**，但参考实现 **libwayland-client** 依然是目前最流行的。不同语言都有很多针对 libwayland-client 的绑定 —— 例如 Python、C++、Rust、Haskell，甚至还有 Objective-C —— 不过在本指南中，我们将使用最原始的 **C 版本**。

Wayland 在技术上并不限于 Linux，它也可以运行在任何类 Unix 系统上。但为了保持简单，我们将在 Linux 上进行学习。

+ 你首先需要一个可用的 **C 编译器**。我将使用 **GCC**，但如果你愿意，也完全可以用 **Clang** 来代替。
+ 接下来，你需要安装 **wayland-client** 库。你所用的发行版大概率已经提供了该库，通常包名是 `wayland`、`libwayland-client` 或 `libwayland-client0`。而且在很多情况下，它可能已经安装好了。太棒了。
+ 你还需要 **libwayland-client 的开发文件**，尤其是 `wayland-client.h` 头文件。它通常会随上面提到的库一同提供，或者单独存在于 `wayland-devel`、`libwayland-dev` 之类的包中。
+ 要运行你的程序，你需要一个可用的 **Wayland 合成器（compositor）**。像 Arch、Fedora（自 25 版起）、Ubuntu（自 17.10 起）这样的主流发行版，默认都提供了基于 Wayland 的 GNOME，所以如果你使用这些发行版，一切应该开箱即用。否则，获取 Wayland 合成器最简单的方法是安装 **Weston**，它是 Wayland 的参考服务器实现。你可以在独立的 **TTY** 上运行 Weston，也可以作为 X 下的一个窗口运行。如果选择后者，请确保从合成器内运行的终端模拟器中启动你的程序，以便它能正确获取所需的环境变量（或者你也可以手动设置它们）。

注：

+ deepin Linux 的 TreeLand 是基于 Wayland 的窗口管理器，本文所有代码均在 deepin Linux  V25 下验证通过。
+ 在 deepin Linux 上使用如下命令安装开发所需要的库和头文件：

```plain
$ sudo apt install libwayland-dev build-essential
```
+ 如果在 deepin Linux 下不切换到 Treeland，可以启动 weston 程序，然后运行 Wayland 客户端程序：

```
$ weston &
$ WAYLAND_DISPLAY=wayland-1 ./my-wayland-app
```

### 第一步
我们先把下面的代码写到 **main.c** 里：

```c
#include <stdio.h>
#include <wayland-client.h>

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (display) {
        printf("Connected!\n");
    } else {
        printf("Error connecting ;(\n");
        return 1;
    }

    wl_display_disconnect(display);
    return 0;
}
```

用下面的命令编译并运行：

```bash
$ gcc main.c -l wayland-client -o runme
$ ./runme
Connected!
```

**恭喜！** 我们已经成功写出了第一个 Wayland 程序。

### Wayland 的基本原理
Wayland 是一种 **客户端-服务器协议**。想要在屏幕上显示图形的客户端（例如需要展示图形界面的应用程序），会连接并与服务器通信。服务器也被称为 **Wayland 合成器（compositor）**，因为它负责将客户端的内容（例如多个窗口）组合在一起，形成最终输出并显示在屏幕上。

合成器还可能会对客户端的内容应用一些变换。例如，它可能会缩放窗口，甚至在三维空间中旋转窗口，以实现某种“概览模式”；或者应用一些酷炫的效果，比如“晃动的窗口”。不过这些都与客户端无关：客户端完全不知道合成器对它们提供的内容做了哪些变换。

Wayland 分为多个层次。  
**线缆格式（Wire format）** 规定了数据是如何被序列化、传输和反序列化的。由于我们会使用抽象掉这些细节的库，所以这里不需要关心这些实现细节。

线缆格式还规定了通信是通过 **Unix 域套接字流** 完成的。套接字通常位于类似 `/run/user/1000/wayland-0` 的路径（更准确地说是 `$XDG_RUNTIME_DIR/$WAYLAND_DISPLAY`）。套接字由服务器创建，通常是在它启动时创建。因为大多数情况下是服务器派生（fork）出客户端，所以它通常会为客户端设置好 `WAYLAND_DISPLAY` 环境变量，以便客户端知道该连接到哪里。

接下来，Wayland 定义了 **对象模型**。Wayland 是 **面向对象** 的：客户端和服务器之间的所有通信都以在某些对象上调用方法的形式来表达。这些对象只是出于理解方便的抽象，本身并不存在——不过客户端和服务器可能会保存一些关于它们的元数据。

对象的方法分为两类：**请求（requests）** 和 **事件（events）**。

+ 请求是由客户端调用的方法。
+ 事件是由服务器发出的。

例如，一个 `wl_pointer` 对象有 `wl_pointer.set_cursor` 请求（修改光标图像），以及 `wl_pointer.motion` 事件（表示指针移动）。

无论是请求还是事件，都可以像编程语言中的函数/方法调用一样，携带额外的数据（参数）。例如，`wl_pointer.motion` 事件会携带时间和两个坐标参数。

但是，Wayland 中的方法既没有返回值，也没有响应机制。换句话说，没有办法从方法调用中得到直接返回结果。要理解原因，需要先掌握以下三个相关概念：

1. **Wayland 是基于消息的**。  
方法调用会以消息的形式在客户端和服务器之间传输（方向取决于是请求还是事件）。一个消息包含对象的 ID、方法的操作码（即静态已知的 ID）以及参数。消息内容的具体布局由线缆格式规定。
2. **发送消息不会阻塞客户端或服务器**。  
换句话说，没有往返延迟（roundtrip delay）。避免往返延迟是 Wayland 设计的目标之一，因为往返延迟会严重拖慢通信。Wayland 的目标是保持极高的速度和效率。
3. **因此，Wayland 是异步的**。  
当你调用一个方法时，可以立即继续执行，而不会得到返回值或确认。如果某个方法在逻辑上需要响应，它通常会通过另一种方法调用来实现。例如，客户端可以用 `wl_shell_surface.set_fullscreen` 请求要求一个窗口全屏化，合成器随后会通过 `wl_shell_surface.configure` 事件回应，并传递新的窗口尺寸（即屏幕大小）。

客户端和服务器的逻辑也必须是异步的。比如 `set_fullscreen` 和 `configure` 之间的延迟，可能不仅仅是处理时间的开销，还可能涉及用户交互（服务器可能会先询问用户是否允许窗口全屏）。因此，客户端应该把 `set_fullscreen` 当作一个“希望将来全屏”的指示，然后继续正常运行，并在任何时候接收到 `configure` 事件时作出反应（比如调整窗口大小），而不必关心事件具体的触发原因。

另一个常见模式是 **创建并返回新对象** 的函数。这可以用同样的请求/事件机制来实现。但由于 Wayland 对象本身不存储数据，客户端和服务器唯一需要协商的是新对象的 **ID**。如果让服务器生成 ID 并传回客户端，就会产生往返延迟。为了解决这个问题，在 Wayland 中是客户端生成并传递新对象的 ID，作为创建请求的参数交给服务器。这样就避免了等待，客户端可以立刻继续执行，并且马上对新对象发起请求。

有时也会反过来：某些场景下会有事件/请求对（例如 `wl_shell_surface.ping` 和 `wl_shell_surface.pong`）；而当服务器创建新对象时，它会将对象 ID 作为事件参数发送给客户端。

最后，Wayland 定义了一些 **具体的对象类型（接口）** 及其可调用的方法，这被称为 **Wayland 核心协议**。例如，存在 `wl_surface` 接口，它提供 `wl_surface.attach` 请求。

与前面提到的层次不同，核心协议是 **可扩展** 的，可以在其上添加新的接口。比较著名的扩展是 **xdg-shell**，我们会在下一节进行讨论。

### wayland-client 库
实现 Wayland 客户端最简单的方式就是使用官方的 **wayland-client** 库。它的作用主要是帮我们屏蔽底层的 **线缆格式（wire format）** 细节。

要使用它，先包含头文件：

```c
#include <wayland-client.h>
```

然后在编译时通过参数 `-l wayland-client` 链接库：

```bash
$ gcc main.c -l wayland-client
```

为了管理对象，**wayland-client** 会在一些命名合理的不透明结构（opaque structures）中保存与对象相关的元数据。我们始终通过 **指针** 来使用这些结构体实例，而无需关心内部细节。你可以把这些结构体实例当作它们所代表的 Wayland 对象来使用。

举个例子，请求（request）的调用就是普通的函数调用：传入对象指针和请求参数即可：

```c
struct wl_shell_surface *shell_surface = ...;
wl_shell_surface_set_title(shell_surface, "Hello World!");
```

这里我们在一个 `wl_shell_surface` 对象上调用了 `wl_shell_surface.set_title` 请求。这个请求只有一个参数，即作为 UTF-8 编码字符串的新标题。

为了响应服务器发出的事件（events），我们需要设置事件处理器。API 非常直观：

```c
void surface_enter_handler(void *data, struct wl_surface *surface, struct wl_output *output)
{
    printf("enter\n");
}
void surface_leave_handler(void *data, struct wl_surface *surface, struct wl_output *output)
{
    printf("leave\n");
}

...

struct wl_surface *surface = ...;
struct wl_surface_listener listener = {
    .enter = surface_enter_handler,
    .leave = surface_leave_handler
};
wl_surface_add_listener(surface, &listener, NULL);
```

这里我们为一个 `wl_surface` 对象建立了事件处理器。该对象可能触发两个事件：`enter` 和 `leave`，它们都带有一个 `wl_output` 类型的参数。

我们需要创建一个 **listener 结构体**，里面存放指向事件处理函数的指针，然后把它的地址传给 `add listener` 函数（注意：这个指针在对象存活期间必须保持有效）。传入的第三个参数（这里只是 `NULL`）会作为处理函数的第一个参数 `void *data`。处理函数的第二个参数是触发事件的对象，这样就可以为多个对象设置同一套事件处理器。其余参数就是事件自带的参数。

当一个请求带有 `new_id` 参数（即由该请求创建的新对象 ID）时，相关函数会返回一个表示新对象的结构体指针。例如：

```c
struct wl_shm *shm = ...;
struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
```

这里我们通过 `wl_shm.create_pool` 请求构建了一个新的 `wl_shm_pool` 对象。这个请求有 `new_id`、`fd` 和 `size` 三个参数。与其传递 `new_id`，我们直接得到了一个新的对象指针作为返回值。

### 全局对象
正如我之前提到的，Wayland 是面向对象的，这意味着它的核心就是对象。对象有不同的类型（称为接口），其中一些类型可以有多个实例，比如 **wl_buffer**（客户端需要多少缓冲区就可以有多少个）。而另一些则只有一个实例（这种设计模式称为单例 singleton），例如 **wl_compositor** 只能有一个。还有一些接口介于两者之间，比如 **wl_output**，通常会有一组固定的显示器与之对应。

这就引出了 **全局对象（global objects）** 的概念。全局对象代表了合成器（compositor）以及其运行环境的属性。大多数全局对象是相应 API 集的入口点。接下来我们深入了解一下，并列出它们。

把下面的程序保存到 `main.c`：

```c
#include <stdio.h>
#include <wayland-client.h>

void registry_global_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name,
    const char *interface,
    uint32_t version
) {
    printf("interface: '%s', version: %u, name: %u\n", interface, version, name);
}

void registry_global_remove_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {
    printf("removed: %u\n", name);
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    struct wl_registry_listener registry_listener = {
        .global = registry_global_handler,
        .global_remove = registry_global_remove_handler
    };
    wl_registry_add_listener(registry, &registry_listener, NULL);

    while (1) {
        wl_display_dispatch(display);
    }
}
```

编译并运行：

```bash
$ gcc main.c -l wayland-client -o runme
$ ./runme
interface: 'wl_compositor', version: 5, name: 1
interface: 'wl_subcompositor', version: 1, name: 2
interface: 'wp_viewporter', version: 1, name: 3
interface: 'zxdg_output_manager_v1', version: 2, name: 4
interface: 'wp_presentation', version: 1, name: 5
interface: 'wp_single_pixel_buffer_manager_v1', version: 1, name: 6
interface: 'wp_tearing_control_manager_v1', version: 1, name: 7
interface: 'zwp_relative_pointer_manager_v1', version: 1, name: 8
interface: 'zwp_pointer_constraints_v1', version: 1, name: 9
interface: 'zwp_input_timestamps_manager_v1', version: 1, name: 10
interface: 'weston_capture_v1', version: 1, name: 11
interface: 'wl_data_device_manager', version: 3, name: 12
interface: 'wl_shm', version: 2, name: 13
interface: 'wl_drm', version: 2, name: 14
interface: 'wl_seat', version: 7, name: 15
interface: 'zwp_linux_dmabuf_v1', version: 4, name: 16
interface: 'zwp_linux_explicit_synchronization_v1', version: 2, name: 17
interface: 'wl_output', version: 4, name: 18
interface: 'zwp_input_panel_v1', version: 1, name: 19
interface: 'zwp_input_method_v1', version: 1, name: 20
interface: 'zwp_text_input_manager_v1', version: 1, name: 21
interface: 'xdg_wm_base', version: 5, name: 22
interface: 'weston_desktop_shell', version: 1, name: 23
^C
```

这样我们就写了一个简化版的 `weston-info` 命令。你需要用 **Ctrl-C** 来中断程序，因为我们还没有写合适的退出逻辑。

首先，**wl_display** 是一个特殊的全局单例，表示整个连接。它在很多方面都很特殊：这是唯一一个你不需要自己创建的对象，一旦建立连接你就已经拥有它了。Wayland 客户端库通过 `wl_display_connect()` 函数返回它，这个函数名字里虽然有 “display”，但它并不是 `wl_display` 对象的方法调用。同样，`wl_display_dispatch()` 也不是 “wl_display.dispatch” 方法，而仅仅是 wayland-client 提供的函数。

另一方面，`wl_display.get_registry` 就是真正的 Wayland 请求。它使用了 **new_id** 机制，我们得到了 **wl_registry** 对象。

**wl_registry** 也是一个全局单例。它的作用是广播所有其他全局对象。Wayland 并不是通过一个 API 让客户端主动查询服务器状态和环境，而是通过 **registry** 来主动通知客户端，既包括启动时的环境信息，也包括之后的动态变化（比如新显示器的插入）。因此，Wayland 天生就是 **热插拔驱动的（hotplug-based）**。同时，这也是客户端获知服务器支持哪些扩展、哪些版本的方式。

注册表通过 **wl_registry.global** 事件广播新的全局对象，通过 **wl_registry.global_remove** 事件广播对象的移除。

目前这些信息在后续会很有用，但现在我们只需要做点“魔法”，拿到几个必须的全局对象：

```c
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
    } else if (strcmp(interface, "wl_shell") == 0) {
        shell = wl_registry_bind(registry, name,
            &wl_shell_interface, 1);
    }
}
```

这里 `wl_registry.global` 传入的 “name” 还不是对象的真正 ID，我们需要用 **wl_registry_bind()** 来进行绑定，生成实际的对象。由于它需要创建类型在编译期未知的新对象，所以 `wayland-client` 的函数签名和底层的 `wl_registry.bind` 请求略有不同。

把 **registry_listener** 的定义移出 `main()`：

```c
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
```

接着在 `main()` 中等待全局事件的初始化通知：

```c
int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // 等待“初始”的全局对象出现
    wl_display_roundtrip(display);

    // 我们需要的对象应该已经就绪！
    if (compositor && shm && shell) {
        printf("Got them all!\n");
    } else {
        printf("Some required globals unavailable\n");
        printf("compositor:%p, shm:%p, shell:%p\n", compositor, shm, shell);
        return 1;
    }

    while (1) {
        wl_display_dispatch(display);
    }
}
```

这里的 **wl_display_roundtrip**（它不是 `wl_display` 的请求，而是一个 wayland-client 提供的特殊函数，底层用的是 `wl_display.sync` 请求）会阻塞客户端，直到所有挂起的方法（请求和事件）都完成，且所有事件监听器都执行完毕。

编译并运行：

```bash
$ gcc main.c -l wayland-client -o runme
$ ./runme
Some required globals unavailable
compositor:0x558d1346a470, shm:0x558d1346a640, shell:(nil)
```

备注：
+ wl_shell 曾经是 Wayland 上管理窗口角色（toplevel、popup 等）的接口。
+ 后来社区发现它设计不合理，协议被冻结并被 xdg_shell 取代。
+ 新 compositor 不会再向客户端公开 wl_shell，所以 wl_registry 遍历时拿不到它 → (nil)。

很好！🎉