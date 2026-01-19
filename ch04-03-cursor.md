## 光标（Cursors）

你可能注意到，当光标悬停在我们的 surface 上时，行为有些异常。这是因为光标进入 surface 后，其图像实际上是未定义的，我们需要每次显式设置它。

设置光标图像其实很简单，只需调用 `wl_pointer::set_cursor` 并传入一个 surface 参数（以及其他一些参数）。这会让该 surface 担任光标的角色。

但在此之前，我们需要先获取 `wl_pointer` 对象：

```c
struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_seat *seat;
struct xdg_wm_base *wm_base = NULL;

struct wl_pointer *pointer;

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
    } else if (strcmp(interface, "wl_seat") == 0) {
        seat = wl_registry_bind(registry, name,
            &wl_seat_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, 1);
    }
}
```

接下来，定义指针事件的处理函数：

```c
void pointer_enter_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
)
{
    wl_pointer_set_cursor(pointer, serial, /* TODO */);
}

void pointer_leave_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface
)
{ }

void pointer_motion_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    wl_fixed_t x,
    wl_fixed_t y
)
{ }

void pointer_button_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state
)
{ }

void pointer_axis_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value
)
{ }

const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter_handler,
    .leave = pointer_leave_handler,
    .motion = pointer_motion_handler,
    .button = pointer_button_handler,
    .axis = pointer_axis_handler
};
```

在 `main()` 中：

```c
// 等待初始全局对象出现
wl_display_roundtrip(display);

// 这仅在计算机有指点设备时有效
pointer = wl_seat_get_pointer(seat);
wl_pointer_add_listener(pointer, &pointer_listener, NULL);
```

此时，我们可以创建一个新的 surface，自行渲染内容并将其作为光标图像。为了简化操作，我们使用一个名为 **wayland-cursor** 的库，它可以加载系统中标准的光标（通常位于 `/usr/share/icons/ThemeName/cursors`）。

注意，`wayland-cursor` 与 `wayland-client` 是两个独立的库，因此需要单独链接：

```makefile
runme: main.c xdg-shell.h xdg-shell.c
        gcc main.c xdg-shell.c -l wayland-client -l wayland-cursor -o runme
```

并在代码中包含头文件：

```c
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "xdg-shell.h"
```

从默认主题加载光标图像的步骤如下：

```c
// 全局变量
struct wl_surface *cursor_surface;
struct wl_cursor_image *cursor_image;

// main() 中代码
struct wl_cursor_theme *cursor_theme =
    wl_cursor_theme_load(NULL, 24, shm); // NULL 表示默认主题，24 是像素大小
struct wl_cursor *cursor =
    wl_cursor_theme_get_cursor(cursor_theme, "left_ptr"); // 获取名为 "left_ptr" 的光标
cursor_image = cursor->images[0]; // 对于动画光标，我们这里只使用第一个图像
struct wl_buffer *cursor_buffer =
    wl_cursor_image_get_buffer(cursor_image);

cursor_surface = wl_compositor_create_surface(compositor);
wl_surface_attach(cursor_surface, cursor_buffer, 0, 0);
wl_surface_commit(cursor_surface);
```

我们将光标 surface 和图像声明为全局变量，以便在 `wl_pointer::enter` 事件处理函数中访问。

`wl_cursor_theme_load` 接受主题名称（传 NULL 使用默认）、光标大小（像素）和 `struct wl_shm *`（用于分配缓冲区）。`wl_cursor_theme_get_cursor` 用于获取光标对象，每个光标对象可能包含多个 `struct wl_cursor_image`（用于动画），这里为了简化，我们只使用第一个。

准备好 surface 后，使用 `wl_pointer::set_cursor` 就非常简单了：

```c
void pointer_enter_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
)
{
    wl_pointer_set_cursor(pointer, serial, cursor_surface,
        cursor_image->hotspot_x, cursor_image->hotspot_y);
}
```

现在编译并运行程序，当光标悬停在 surface 上时，你应该能看到正常的箭头光标。你也可以尝试使用其他光标图像，而不是 `"left_ptr"`。
