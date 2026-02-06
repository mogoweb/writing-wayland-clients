#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/input-event-codes.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"

#define WIDTH 640
#define HEIGHT 480
#define TITLEBAR_HEIGHT 30
#define BUTTON_WIDTH 40

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;

    struct wl_seat *seat;
    struct wl_pointer *pointer;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct wl_buffer *buffer;

    int running;
    int maximized;

    int pointer_x;
    int pointer_y;
    uint32_t last_serial;

    int width;
    int height;
};

/* ---------------- shm helper ---------------- */

static void
buffer_release(void *data, struct wl_buffer *buffer)
{
    wl_buffer_destroy(buffer);
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release
};

static int
create_shm_file(off_t size)
{
    char name[] = "/wayland-shm-XXXXXX";
    int fd = shm_open(name, O_RDWR | O_CREAT | O_EXCL, 0600);
    if (fd < 0)
        return -1;

    shm_unlink(name);

    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* ---------------- drawing ---------------- */

static void
draw_frame(struct app_state *state)
{
    int stride = state->width * 4;
    int size = stride * state->height;

    int fd = create_shm_file(size);
    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    for (int y = 0; y < state->height; y++) {
        for (int x = 0; x < state->width; x++) {
            uint32_t color = 0xFFFFFFFF;

            if (y < TITLEBAR_HEIGHT) {
                color = 0xFF444444;

                if (x > state->width - BUTTON_WIDTH)
                    color = 0xFFFF0000;       // close
                else if (x > state->width - 2 * BUTTON_WIDTH)
                    color = 0xFF00FF00;       // maximize
                else if (x > state->width - 3 * BUTTON_WIDTH)
                    color = 0xFFFFFF00;       // minimize
            }

            data[y * state->width + x] = color;
        }
    }

    struct wl_shm_pool *pool =
        wl_shm_create_pool(state->shm, fd, size);

    state->buffer =
        wl_shm_pool_create_buffer(pool, 0,
                                  state->width, state->height,
                                  stride,
                                  WL_SHM_FORMAT_XRGB8888);
    wl_buffer_add_listener(state->buffer,
                       &buffer_listener,
                       NULL);

    wl_shm_pool_destroy(pool);
    close(fd);

    wl_surface_attach(state->surface, state->buffer, 0, 0);
    wl_surface_damage(state->surface, 0, 0, state->width, state->height);
    wl_surface_commit(state->surface);

    munmap(data, size);
}

/* ---------------- pointer logic ---------------- */

static void
handle_click(struct app_state *state, int x, int y, uint32_t serial)
{
    if (y >= TITLEBAR_HEIGHT)
        return;

    if (x > state->width - BUTTON_WIDTH) {
        printf("close\n");
        state->running = 0;
    } else if (x > state->width - 2 * BUTTON_WIDTH) {
        printf("maximize / restore\n");
        if (state->maximized)
            xdg_toplevel_unset_maximized(state->xdg_toplevel);
        else
            xdg_toplevel_set_maximized(state->xdg_toplevel);

        state->maximized = !state->maximized;
    } else if (x > state->width - 3 * BUTTON_WIDTH) {
        printf("minimize\n");
        xdg_toplevel_set_minimized(state->xdg_toplevel);
    } else {
        printf("move\n");
        xdg_toplevel_move(state->xdg_toplevel,
                          state->seat,
                          serial);
    }
}

static void
pointer_enter(void *data,
              struct wl_pointer *pointer,
              uint32_t serial,
              struct wl_surface *surface,
              wl_fixed_t sx,
              wl_fixed_t sy)
{
    struct app_state *state = data;
    state->last_serial = serial;
    state->pointer_x = wl_fixed_to_int(sx);
    state->pointer_y = wl_fixed_to_int(sy);
}

static void pointer_leave(void *data, struct wl_pointer *wl_pointer, uint32_t serial, struct wl_surface *surface) {}

static void
pointer_motion(void *data,
               struct wl_pointer *pointer,
               uint32_t time,
               wl_fixed_t sx,
               wl_fixed_t sy)
{
    struct app_state *state = data;
    state->pointer_x = wl_fixed_to_int(sx);
    state->pointer_y = wl_fixed_to_int(sy);
}

static void
pointer_button(void *data,
               struct wl_pointer *pointer,
               uint32_t serial,
               uint32_t time,
               uint32_t button,
               uint32_t state)
{
    struct app_state *state_app = data;

    if (button == BTN_LEFT &&
        state == WL_POINTER_BUTTON_STATE_PRESSED) {

        handle_click(state_app,
                     state_app->pointer_x,
                     state_app->pointer_y,
                     serial);
    }
}

static void pointer_axis(void *data,
                         struct wl_pointer *pointer,
                         uint32_t time,
                         uint32_t axis,
                         wl_fixed_t value) {}

static void pointer_frame(void *data,
                          struct wl_pointer *pointer) {}

static void pointer_axis_source(void *data,
                                struct wl_pointer *pointer,
                                uint32_t source) {}

static void pointer_axis_stop(void *data,
                              struct wl_pointer *pointer,
                              uint32_t time,
                              uint32_t axis) {}

static void pointer_axis_discrete(void *data,
                                  struct wl_pointer *pointer,
                                  uint32_t axis,
                                  int32_t discrete) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter  = pointer_enter,
    .leave  = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis          = pointer_axis,
    .frame         = pointer_frame,
    .axis_source   = pointer_axis_source,
    .axis_stop     = pointer_axis_stop,
    .axis_discrete = pointer_axis_discrete,
};

/* ---------------- xdg-shell ---------------- */

static void
xdg_wm_base_ping(void *data,
                 struct xdg_wm_base *wm,
                 uint32_t serial)
{
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping
};

static void
xdg_surface_configure(void *data,
                      struct xdg_surface *surface,
                      uint32_t serial)
{
    xdg_surface_ack_configure(surface, serial);
    draw_frame(data);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

static void
xdg_toplevel_configure(void *data,
                       struct xdg_toplevel *toplevel,
                       int32_t width,
                       int32_t height,
                       struct wl_array *states)
{
    struct app_state *state = data;
    if (width > 0 && height > 0) {
        state->width  = width;
        state->height = height;
    }
}

static void
xdg_toplevel_close(void *data,
                   struct xdg_toplevel *toplevel)
{
    ((struct app_state *)data)->running = 0;
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close     = xdg_toplevel_close
};

/* ---------------- registry ---------------- */

static void
registry_global(void *data,
                struct wl_registry *registry,
                uint32_t name,
                const char *interface,
                uint32_t version)
{
    struct app_state *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0)
        state->compositor =
            wl_registry_bind(registry, name,
                             &wl_compositor_interface, 4);

    else if (strcmp(interface, wl_shm_interface.name) == 0)
        state->shm =
            wl_registry_bind(registry, name,
                             &wl_shm_interface, 1);

    else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base =
            wl_registry_bind(registry, name,
                             &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base,
                                 &wm_base_listener,
                                 state);
    }
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat =
            wl_registry_bind(registry, name,
                             &wl_seat_interface, 7);
        state->pointer = wl_seat_get_pointer(state->seat);
        wl_pointer_add_listener(state->pointer,
                                &pointer_listener,
                                state);
    }
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global,
    .global_remove = NULL
};

/* ---------------- main ---------------- */

int
main(void)
{
    struct app_state state = {
        .running = 1,
        .width = WIDTH,
        .height = HEIGHT,
    };

    state.display = wl_display_connect(NULL);
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry,
                             &registry_listener,
                             &state);
    wl_display_roundtrip(state.display);

    state.surface =
        wl_compositor_create_surface(state.compositor);

    state.xdg_surface =
        xdg_wm_base_get_xdg_surface(state.xdg_wm_base,
                                    state.surface);

    xdg_surface_add_listener(state.xdg_surface,
                             &xdg_surface_listener,
                             &state);

    state.xdg_toplevel =
        xdg_surface_get_toplevel(state.xdg_surface);

    xdg_toplevel_add_listener(state.xdg_toplevel,
                              &xdg_toplevel_listener,
                              &state);

    xdg_toplevel_set_title(state.xdg_toplevel,
                           "Wayland CSD Demo");

    wl_surface_commit(state.surface);

    while (state.running &&
           wl_display_dispatch(state.display) != -1) {
    }

    wl_display_disconnect(state.display);
    return 0;
}
