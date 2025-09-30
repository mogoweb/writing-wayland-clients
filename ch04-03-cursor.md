### Cursors

You may have noticed that the cursor behaves somewhat strangely when it's hovered over our surface. This is because the cursor image is actually undefined once it enters the surface, and we need to explicitly set it each time.

Setting a cursor image is actually pretty easy. You just call `wl_pointer::set_cursor` with a surface argument (and a few others). This gives that surface the role of a cursor.

But to do that, we need to get the `wl_pointer` object first:

```c
struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_seat *seat;
struct zxdg_shell_v6 *xdg_shell;

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
    } else if (strcmp(interface, "zxdg_shell_v6") == 0) {
        xdg_shell = wl_registry_bind(registry, name,
            &zxdg_shell_v6_interface, 1);
    }
}
```

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

and then in `main()`:

```c
// wait for the "initial" set of globals to appear
wl_display_roundtrip(display);

// this is only going to work if your computer
// has a pointing device
pointer = wl_seat_get_pointer(seat);
wl_pointer_add_listener(pointer, &pointer_listener, NULL);
```

At this point, we could just create a new surface, render something ourselves and make it the cursor image. Instead of that, we're going to use a library called **wayland-cursor** that handles loading the standard cursors from the system (they are in `/usr/share/icons/ThemeName/cursors`).

Despite using a similar naming scheme, `wayland-cursor` is a separate library to `wayland-client`, so we need to link it separately:

```makefile
runme: main.c xdg-shell.h xdg-shell.c
        gcc main.c xdg-shell.c -l wayland-client -l wayland-cursor -o runme
```

and include its header:

```c
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "xdg-shell.h"
```

The steps to load a simple cursor image from the default theme are as follows:

```c
// global variables:
struct wl_surface *cursor_surface;
struct wl_cursor_image *cursor_image;

// code in main():
struct wl_cursor_theme *cursor_theme =
    wl_cursor_theme_load(NULL, 24, shm);
struct wl_cursor *cursor =
    wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
cursor_image = cursor->images[0];
struct wl_buffer *cursor_buffer =
    wl_cursor_image_get_buffer(cursor_image);

cursor_surface = wl_compositor_create_surface(compositor);
wl_surface_attach(cursor_surface, cursor_buffer, 0, 0);
wl_surface_commit(cursor_surface);
```

I've declared the surface and the image to be global to make it easier for us to access them from the `wl_pointer::enter` handler. The `wl_cursor_theme_load` function accepts the theme name (we pass `NULL` to get the default one), the size (in pixels) that we want to use and a `struct wl_shm *` to allocate buffers. We pass the cursor name to `wl_cursor_theme_get_cursor`. Each `struct wl_cursor` can have multiple `struct wl_cursor_image`s in case it's animated, but we're only going to use the first one here for simplicity.

Once we've prepared the surface, it's very easy to use `wl_pointer::set_cursor`:

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

If you compile and run the program now, you should see the normal arrow cursor when hovering over the surface. You can also experiment with using other images instead of `"left_ptr"`.
