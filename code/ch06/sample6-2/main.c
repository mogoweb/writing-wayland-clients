#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <cairo/cairo.h>
#include <poll.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "sni.h"

// 状态结构体
struct app_state {
    struct wl_display *display;
    struct wl_shm *shm;
    struct wl_compositor *compositor;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *deco_manager;
    struct zxdg_toplevel_decoration_v1 *deco;

    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    
    int width, height;
    int running;
    int visible;

    cairo_surface_t *my_icon_surface;
};

// --- Cairo 绘图辅助 ---
static struct wl_buffer* create_shm_buffer(struct app_state *app) {
    char name[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(name);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed\n");
        return NULL;
    }

    unlink(name);
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, app->width);
    int size = stride * app->height;
    ftruncate(fd, size);

    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    struct wl_shm_pool *pool = wl_shm_create_pool(app->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, app->width, app->height, stride, WL_SHM_FORMAT_ARGB8888);
    
    // 使用 Cairo 绘制纯色背景 (例如：天蓝色)
    cairo_surface_t *surface = cairo_image_surface_create_for_data(data, CAIRO_FORMAT_ARGB32, app->width, app->height, stride);
    cairo_t *cr = cairo_create(surface);
    cairo_set_source_rgb(cr, 0.53, 0.81, 0.98); // SkyBlue
    cairo_paint(cr);
    
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
    munmap(data, size);
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

// --- Wayland 回调 ---
static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height, struct wl_array *states) {
    // TODO:
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct app_state *app = data;
    printf("点击了关闭按钮，隐藏窗口并驻留托盘...\n");
    
    // 隐藏窗口逻辑：解绑角色
    if (app->deco) zxdg_toplevel_decoration_v1_destroy(app->deco);
    if (app->deco_manager) zxdg_decoration_manager_v1_destroy(app->deco_manager);
    xdg_toplevel_destroy(app->xdg_toplevel);
    xdg_surface_destroy(app->xdg_surface);
    app->xdg_toplevel = NULL;
    app->xdg_surface = NULL;
    app->visible = 0;
    wl_surface_attach(app->surface, NULL, 0, 0);
    wl_surface_commit(app->surface);
}

static const struct xdg_toplevel_listener toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct app_state *app = data;
    // 必须确认配置事件
    xdg_surface_ack_configure(xdg_surface, serial);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// --- 全局注册 ---
static void registry_handle_global(void *data, struct wl_registry *reg, uint32_t id, const char *interface, uint32_t version) {
    struct app_state *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        app->compositor = wl_registry_bind(reg, id, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        app->shm = wl_registry_bind(reg, id, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        app->wm_base = wl_registry_bind(reg, id, &xdg_wm_base_interface, 1);
    else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        app->deco_manager = wl_registry_bind(reg, id, &zxdg_decoration_manager_v1_interface, 1);
}

int main() {
    struct app_state app = { .width = 400, .height = 300, .running = 1, .visible = 1 };
    
    app.display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(app.display);
    static const struct wl_registry_listener reg_listener = { .global = registry_handle_global };
    wl_registry_add_listener(registry, &reg_listener, &app);
    wl_display_roundtrip(app.display);

    // 创建 Surface
    app.surface = wl_compositor_create_surface(app.compositor);
    app.xdg_surface = xdg_wm_base_get_xdg_surface(app.wm_base, app.surface);
    xdg_surface_add_listener(app.xdg_surface, &xdg_surface_listener, &app);
    app.xdg_toplevel = xdg_surface_get_toplevel(app.xdg_surface);
    xdg_toplevel_add_listener(app.xdg_toplevel, &toplevel_listener, &app);

    // 请求 SSD (服务端装饰)
    if (app.deco_manager) {
        app.deco = zxdg_decoration_manager_v1_get_toplevel_decoration(app.deco_manager, app.xdg_toplevel);
        zxdg_toplevel_decoration_v1_set_mode(app.deco, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }

    wl_surface_commit(app.surface);
    wl_display_roundtrip(app.display);

    // 渲染第一帧
    struct wl_buffer *buffer = create_shm_buffer(&app);
    wl_surface_attach(app.surface, buffer, 0, 0);
    wl_surface_commit(app.surface);

    // 初始化 SNI 托盘
    sni_manager_t *sni = sni_manager_create("org.deepin.waylanddemo.tray", "utilities-terminal");

    struct pollfd fds[1];
    fds[0].fd = wl_display_get_fd(app.display);
    fds[0].events = POLLIN;

    // 主循环
    while (app.running/* && wl_display_dispatch(app.display) != -1*/) {
        // --- STEP 1: 准备读取 Wayland 事件 ---
        // 这步非常关键，它告诉 Wayland 库我们要开始处理事件了
        while (wl_display_prepare_read(app.display) != 0) {
            wl_display_dispatch_pending(app.display);
        }

        // --- STEP 2: 发送所有挂起的 Wayland 请求 ---
        wl_display_flush(app.display);

        // --- STEP 3: 使用 poll 同时监控事件 ---
        // 设置超时时间（例如 10 毫秒），这样 D-Bus 才有机会被处理
        int ret = poll(fds, 1, 10);

        if (ret > 0 && (fds[0].revents & POLLIN)) {
            // 有 Wayland 数据可读
            wl_display_read_events(app.display);
        } else {
            // 没有数据或超时，取消读取准备
            wl_display_cancel_read(app.display);
        }

        // 分发处理 Wayland 事件
        wl_display_dispatch_pending(app.display);

        // --- STEP 4: 处理 D-Bus 消息 ---
        sni_manager_dispatch(sni);
    }

    return 0;
}
