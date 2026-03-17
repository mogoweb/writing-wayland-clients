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
#include "xdg-decoration-unstable-v1-client-protocol.h"

struct app_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_exporter_v2 *exporter;
    struct zxdg_decoration_manager_v1 *deco_manager;
    
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    
    char *exported_handle;
    bool wait_for_configure;
    bool running;
};

/* --- 极简的 SHM 颜色填充缓冲创建 --- */
static struct wl_buffer* create_shm_buffer(struct app_state *state, int width, int height, uint32_t color) {
    int stride = width * 4;
    int size = stride * height;
    int fd = memfd_create("wayland-shm", 0);
    ftruncate(fd, size);
    
    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < width * height; i++) data[i] = color; // 填充纯色
    
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    
    wl_shm_pool_destroy(pool);
    munmap(data, size);
    close(fd);
    return buffer;
}

static void toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    // 即使我们不需要处理调整大小，Wayland 也要求提供 configure 回调函数，保持为空即可
}

static void toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct app_state *state = data;
    printf("\n[Event -> Toplevel] 用户点击了关闭按钮，程序准备退出...\n");
    // 将运行标志位设为 false，以打破主事件循环
    state->running = false;
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = toplevel_configure,
    .close = toplevel_close,
};

/* --- XDG Shell 监听器 (映射窗口必备) --- */
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    
    // 收到混成器的配置请求后，附加缓冲区并提交以显示窗口
    if (state->wait_for_configure) {
        struct wl_buffer *buffer = create_shm_buffer(state, 600, 400, 0xFFFFFFFF); // 白色
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

/* --- xdg_foreign 监听器 (获取句柄) --- */
static void handle_exported_handle(void *data, struct zxdg_exported_v2 *exported, const char *handle) {
    struct app_state *state = data;
    state->exported_handle = strdup(handle);
    printf("\n>>> 窗口导出成功！\n");
    printf("请在另一个终端运行子进程:\n");
    printf("./wayland_child %s\n\n", handle);
}
static const struct zxdg_exported_v2_listener exported_listener = { .handle = handle_exported_handle };

/* --- 全局注册表监听器 --- */
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct app_state *state = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        state->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
    else if (strcmp(interface, zxdg_exporter_v2_interface.name) == 0)
        state->exporter = wl_registry_bind(registry, name, &zxdg_exporter_v2_interface, 1);
    else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        state->deco_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
}
static const struct wl_registry_listener registry_listener = { .global = registry_global };

int main() {
    struct app_state state = {0};
    state.wait_for_configure = true;
    state.running = true;

    state.display = wl_display_connect(NULL);
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    xdg_wm_base_add_listener(state.wm_base, &wm_base_listener, &state);

    // 1. 创建并映射父窗口
    state.surface = wl_compositor_create_surface(state.compositor);
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.xdg_toplevel, &toplevel_listener, &state);
    xdg_toplevel_set_title(state.xdg_toplevel, "App A - Wayland Parent");
    if (state.deco_manager) {
        struct zxdg_toplevel_decoration_v1 *deco = 
            zxdg_decoration_manager_v1_get_toplevel_decoration(state.deco_manager, state.xdg_toplevel);
        // 明确要求混成器为我们画标题栏 (SERVER_SIDE)
        zxdg_toplevel_decoration_v1_set_mode(deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        printf("[Info] 已请求服务端窗口装饰(SSD)。\n");
    } else {
        printf("[Warning] 当前混成器不支持 xdg-decoration 协议 (可能是 GNOME)，窗口可能没有标题栏。\n");
    }
    wl_surface_commit(state.surface);
    
    // 等待窗口在屏幕上映射成功
    while (state.wait_for_configure && wl_display_dispatch(state.display) != -1);

    // 2. 导出窗口 (相当于 Windows 准备 HWND 给别人)
    struct zxdg_exported_v2 *exported = zxdg_exporter_v2_export_toplevel(state.exporter, state.surface);
    zxdg_exported_v2_add_listener(exported, &exported_listener, &state);

    // 保持主循环运行
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // 事件循环
    }

    printf("正在清理资源并退出...\n");
    if (state.exported_handle) free(state.exported_handle);
    zxdg_exported_v2_destroy(exported);
    xdg_toplevel_destroy(state.xdg_toplevel);
    xdg_surface_destroy(state.xdg_surface);
    wl_surface_destroy(state.surface);
    wl_display_disconnect(state.display);

    return 0;
}
