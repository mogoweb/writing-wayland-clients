#include <stdio.h>
#include <string.h>

#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"  // 需要用 wayland-scanner 生成

struct wl_compositor *compositor = NULL;
struct wl_shm *shm = NULL;
struct xdg_wm_base *wm_base = NULL;

struct wl_surface *surface = NULL;
struct wl_buffer *buffer = NULL;

static void registry_global_handler(
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
    } else if (strcmp(interface, "xdg_wm_base") == 0) {
        wm_base = wl_registry_bind(registry, name,
            &xdg_wm_base_interface, 1);
    }
}

static void registry_global_remove_handler(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {
    // 忽略
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

// 处理 wm_base 的 ping
static void xdg_wm_base_ping(void *data,
    struct xdg_wm_base *xdg_wm_base, uint32_t serial)
{
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener wm_base_listener = {
    .ping = xdg_wm_base_ping
};

// xdg_surface configure
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    // 必须 ack configure
    xdg_surface_ack_configure(xdg_surface, serial);

    // attach buffer 并 commit
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "无法连接 Wayland 显示服务器\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // 等待“初始”全局对象出现
    wl_display_roundtrip(display);

    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "缺少必要的全局对象 (compositor/shm/wm_base)\n");
        return -1;
    }

    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    surface = wl_compositor_create_surface(compositor);

    // 使用 xdg_shell 包装 surface
    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);

    // 设置标题（可选）
    xdg_toplevel_set_title(toplevel, "xdg_shell 窗口");

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);

    int width = 200;
    int height = 200;
    int stride = width * 4;
    int size = stride * height;

    // 创建共享内存 buffer
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    ftruncate(fd, size);

    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    memset(data, 0xFF, size); // 填充白色背景

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    buffer = wl_shm_pool_create_buffer(pool,
        0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);

    while (wl_display_dispatch(display) != -1) {
        // 主循环
    }

    return 0;
}
