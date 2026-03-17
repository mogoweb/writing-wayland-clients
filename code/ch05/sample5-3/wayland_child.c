#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-foreign-unstable-v2-client-protocol.h"

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_importer_v2 *importer;
    
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    bool wait_for_configure;
};

/* --- 极简的 SHM 颜色填充缓冲创建 (同 Parent) --- */
static struct wl_buffer* create_shm_buffer(struct app_state *state, int width, int height, uint32_t color) {
    int stride = width * 4;
    int size = stride * height;
    int fd = memfd_create("wayland-shm", 0);
    ftruncate(fd, size);
    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < width * height; i++) data[i] = color;
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool); munmap(data, size); close(fd);
    return buffer;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    if (state->wait_for_configure) {
        struct wl_buffer *buffer = create_shm_buffer(state, 300, 200, 0xFF888888); // 灰色
        wl_surface_attach(state->surface, buffer, 0, 0);
        wl_surface_commit(state->surface);
        state->wait_for_configure = false;
    }
}
static const struct xdg_surface_listener xdg_surface_listener = { .configure = xdg_surface_configure };

static void wm_base_ping(void *data, struct xdg_wm_base *wm_base, uint32_t serial) {
    xdg_wm_base_pong(wm_base, serial);
}
static const struct xdg_wm_base_listener wm_base_listener = { .ping = wm_base_ping };

static void handle_imported_destroyed(void *data, struct zxdg_imported_v2 *imported) {
    printf("\n[Event -> Import] 收到 destroyed 事件！\n");
    printf(">>> 警告: 父窗口已被销毁，当前的导入句柄已失效。\n");
    printf(">>> 子进程将自动退出...\n");
    
    // 清理导入对象
    zxdg_imported_v2_destroy(imported);
    exit(0); // 退出子进程
}
static const struct zxdg_imported_v2_listener imported_listener = {
    .destroyed = handle_imported_destroyed
};

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct app_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (strcmp(interface, zxdg_importer_v2_interface.name) == 0)
        state->importer = wl_registry_bind(registry, name, &zxdg_importer_v2_interface, 1);
}
static const struct wl_registry_listener registry_listener = { .global = registry_global };

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("用法: ./wayland_child <parent_handle>\n");
        return -1;
    }
    const char *parent_handle = argv[1];

    struct app_state state = {0};
    state.wait_for_configure = true;

    state.display = wl_display_connect(NULL);
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    xdg_wm_base_add_listener(state.wm_base, &wm_base_listener, &state);

    // 1. 创建子窗口
    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_set_title(state.xdg_toplevel, "App B - Wayland Child");

    // 2. 导入父进程句柄，并强制声明父子关系 (Wayland 版的 SetWindowLongPtr)
    printf("正在导入句柄: %s...\n", parent_handle);
    struct zxdg_imported_v2 *imported = zxdg_importer_v2_import_toplevel(state.importer, parent_handle);
    zxdg_imported_v2_add_listener(imported, &imported_listener, &state);
    zxdg_imported_v2_set_parent_of(imported, state.surface);

    wl_surface_commit(state.surface);

    // 保持主循环运行
    while (wl_display_dispatch(state.display) != -1) {
        // 事件循环
    }

    return 0;
}
