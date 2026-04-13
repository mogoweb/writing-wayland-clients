#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-dialog-v1-client-protocol.h"

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

/* Application state */
struct wl_display *display = NULL;
struct wl_compositor *compositor = NULL;
struct wl_shm *shm = NULL;
struct xdg_wm_base *wm_base = NULL;
struct xdg_wm_dialog_v1 *wm_dialog = NULL;

/* Parent window state */
struct wl_surface *parent_surface;
struct xdg_surface *parent_xdg_surface;
struct xdg_toplevel *parent_xdg_toplevel;
struct wl_buffer *parent_buffer;

/* Dialog window state */
struct wl_surface *dialog_surface;
struct xdg_surface *dialog_xdg_surface;
struct xdg_toplevel *dialog_xdg_toplevel;
struct xdg_dialog_v1 *dialog;
struct wl_buffer *dialog_buffer;

static void
xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_ping,
};

static void
xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

/* Parent toplevel listeners */
static void
parent_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                          int32_t width, int32_t height, struct wl_array *states) {
}

static void
parent_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    exit(0);
}

static const struct xdg_toplevel_listener parent_toplevel_listener = {
    .configure = parent_toplevel_configure,
    .close = parent_toplevel_close,
};

/* Dialog toplevel listeners */
static void
dialog_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                          int32_t width, int32_t height, struct wl_array *states) {
}

static void
dialog_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    /* When dialog is closed, just hide it by destroying its surface */
    if (dialog_surface) {
        wl_surface_destroy(dialog_surface);
        dialog_surface = NULL;
        dialog = NULL;
    }
}

static const struct xdg_toplevel_listener dialog_toplevel_listener = {
    .configure = dialog_toplevel_configure,
    .close = dialog_toplevel_close,
};

/* Buffer release callback */
static void
buffer_release(void *data, struct wl_buffer *wl_buffer) {
    /* Buffer can be reused */
}

static const struct wl_buffer_listener buffer_listener = {
    .release = buffer_release,
};

/* Create a solid color buffer using Cairo */
static struct wl_buffer *
create_buffer(int width, int height, float r, float g, float b) {
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, width);
    int size = stride * height;

    int fd = allocate_shm_file(size);
    if (fd < 0) {
        fprintf(stderr, "Failed to allocate shm file\n");
        return NULL;
    }

    void *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        fprintf(stderr, "Failed to mmap shm file\n");
        close(fd);
        return NULL;
    }

    /* Create Cairo surface and fill with color */
    cairo_surface_t *surface = cairo_image_surface_create_for_data(
        data, CAIRO_FORMAT_ARGB32, width, height, stride);
    cairo_t *cr = cairo_create(surface);

    cairo_set_source_rgb(cr, r, g, b);
    cairo_paint(cr);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height,
                                                         stride, WL_SHM_FORMAT_ARGB8888);
    wl_buffer_add_listener(buffer, &buffer_listener, NULL);

    wl_shm_pool_destroy(pool);
    close(fd);

    return buffer;
}

/* Registry handler */
static void
global_registry_handler(void *data, struct wl_registry *registry, uint32_t id,
                        const char *interface, uint32_t version) {
    printf("Got a registry event for %s id %d\n", interface, id);
    if (strcmp(interface, "wl_compositor") == 0) {
        compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 1);
    } else if (strcmp(interface, "wl_shm") == 0) {
        shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(wm_base, &xdg_wm_base_listener, NULL);
    } else if (strcmp(interface, "xdg_wm_dialog_v1") == 0) {
        wm_dialog = wl_registry_bind(registry, id, &xdg_wm_dialog_v1_interface, 1);
        printf("Found xdg_wm_dialog_v1 support\n");
    }
}

static void
global_registry_remover(void *data, struct wl_registry *registry, uint32_t id) {
    printf("Got a registry losing event for %d\n", id);
}

static const struct wl_registry_listener registry_listener = {
    global_registry_handler,
    global_registry_remover
};

/* Create parent window */
static void
create_parent_window(void) {
    parent_surface = wl_compositor_create_surface(compositor);
    parent_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, parent_surface);
    xdg_surface_add_listener(parent_xdg_surface, &xdg_surface_listener, NULL);

    parent_xdg_toplevel = xdg_surface_get_toplevel(parent_xdg_surface);
    xdg_toplevel_add_listener(parent_xdg_toplevel, &parent_toplevel_listener, NULL);
    xdg_toplevel_set_title(parent_xdg_toplevel, "Parent Window");

    wl_surface_commit(parent_surface);
    wl_display_roundtrip(display);

    parent_buffer = create_buffer(400, 300, 0.2, 0.5, 0.8); /* Blue */
    if (parent_buffer == NULL) {
        fprintf(stderr, "Failed to create parent buffer\n");
        exit(1);
    }

    wl_surface_attach(parent_surface, parent_buffer, 0, 0);
    wl_surface_damage(parent_surface, 0, 0, 400, 300);
    wl_surface_commit(parent_surface);
    wl_display_flush(display);
}

/* Create dialog window */
static void
create_dialog_window(void) {
    if (dialog_surface != NULL) {
        fprintf(stderr, "Dialog already exists\n");
        return;
    }

    if (wm_dialog == NULL) {
        fprintf(stderr, "xdg_wm_dialog_v1 not supported by compositor\n");
        return;
    }

    dialog_surface = wl_compositor_create_surface(compositor);
    dialog_xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, dialog_surface);
    xdg_surface_add_listener(dialog_xdg_surface, &xdg_surface_listener, NULL);

    dialog_xdg_toplevel = xdg_surface_get_toplevel(dialog_xdg_surface);
    xdg_toplevel_add_listener(dialog_xdg_toplevel, &dialog_toplevel_listener, NULL);
    xdg_toplevel_set_title(dialog_xdg_toplevel, "Modal Dialog");

    /* Set the parent window */
    xdg_toplevel_set_parent(dialog_xdg_toplevel, parent_xdg_toplevel);

    /* Create xdg_dialog_v1 object and mark it as modal */
    dialog = xdg_wm_dialog_v1_get_xdg_dialog(wm_dialog, dialog_xdg_toplevel);
    xdg_dialog_v1_set_modal(dialog);

    printf("Created modal dialog with parent relationship\n");

    wl_surface_commit(dialog_surface);
    wl_display_roundtrip(display);

    dialog_buffer = create_buffer(300, 200, 0.9, 0.3, 0.3); /* Red */
    if (dialog_buffer == NULL) {
        fprintf(stderr, "Failed to create dialog buffer\n");
        return;
    }

    wl_surface_attach(dialog_surface, dialog_buffer, 0, 0);
    wl_surface_damage(dialog_surface, 0, 0, 300, 200);
    wl_surface_commit(dialog_surface);
    wl_display_flush(display);
}

int main(int argc, char **argv) {
    display = wl_display_connect(NULL);
    if (display == NULL) {
        fprintf(stderr, "Can't connect to display\n");
        exit(1);
    }
    printf("connected to display\n");

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_dispatch(display);
    wl_display_roundtrip(display);

    if (compositor == NULL || shm == NULL || wm_base == NULL) {
        fprintf(stderr, "Required globals not available\n");
        exit(1);
    }

    /* Create parent window */
    create_parent_window();

    printf("Parent window created.\n");
    printf("Press Enter to open a modal dialog...\n");
    getchar();

    /* Create dialog window */
    create_dialog_window();

    printf("Dialog created. Press Enter to close...\n");
    getchar();

    /* Clean up */
    if (dialog_surface) {
        wl_surface_destroy(dialog_surface);
    }
    if (parent_surface) {
        wl_surface_destroy(parent_surface);
    }

    wl_display_disconnect(display);
    printf("disconnected from display\n");

    return 0;
}
