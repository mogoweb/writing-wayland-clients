/**
 * Viewporter 协议示例
 *
 * Viewporter 协议允许客户端对 surface 进行裁剪 (cropping) 和缩放 (scaling)，
 * 这使得 buffer 尺寸和 surface 尺寸可以解耦。
 *
 * 核心概念:
 * - 源矩形 (source rectangle): 从 buffer 中裁剪的区域
 * - 目标尺寸 (destination size): surface 的最终显示尺寸
 *
 * 本示例演示:
 * 1. 创建一个大 buffer (400x400)
 * 2. 使用 viewporter 将其裁剪到中心区域 (200x200)
 * 3. 缩放显示为 300x300 的 surface
 */

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
#include "viewporter-client-protocol.h"

// ---------------------------------------------------------
// 全局状态
// ---------------------------------------------------------
struct ClientState {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wp_viewporter *viewporter;
    bool running;
};

struct Window {
    struct ClientState *state;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    struct wp_viewport *viewport;
    int width, height;
    bool is_configured;
};

// ---------------------------------------------------------
// 共享内存绘图辅助函数
// ---------------------------------------------------------
static struct wl_buffer *create_checkerboard_buffer(struct wl_shm *shm, int width, int height) {
    int stride = width * 4;
    int size = stride * height;

    int fd = memfd_create("wayland-shm-buffer", MFD_CLOEXEC);
    ftruncate(fd, size);

    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // 创建棋盘格图案，方便观察裁剪效果
    int cell_size = 50;
    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            int cell_x = x / cell_size;
            int cell_y = y / cell_size;
            // 棋盘格颜色：蓝色和黄色交替
            if ((cell_x + cell_y) % 2 == 0) {
                pixels[y * width + x] = 0xFF3366CC; // 蓝色
            } else {
                pixels[y * width + x] = 0xFFCCCC33; // 黄色
            }
        }
    }

    // 在中心画一个红色方块作为参考点
    int center_x = width / 2;
    int center_y = height / 2;
    int rect_size = 80;
    for (int y = center_y - rect_size/2; y < center_y + rect_size/2; ++y) {
        for (int x = center_x - rect_size/2; x < center_x + rect_size/2; ++x) {
            if (x >= 0 && x < width && y >= 0 && y < height) {
                pixels[y * width + x] = 0xFFFF3333; // 红色
            }
        }
    }

    munmap(pixels, size);

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

// ---------------------------------------------------------
// XDG 事件监听器
// ---------------------------------------------------------
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct Window *win = data;
    xdg_surface_ack_configure(xdg_surface, serial);

    if (!win->is_configured) {
        // buffer 尺寸: 400x400 (棋盘格)
        struct wl_buffer *buffer = create_checkerboard_buffer(win->state->shm, 400, 400);
        wl_surface_attach(win->surface, buffer, 0, 0);

        // 使用 viewporter 进行裁剪和缩放
        if (win->viewport) {
            // 源矩形: 从 buffer 的 (100, 100) 位置开始，取 200x200 区域
            // 这会裁剪掉 buffer 的边缘部分
            wp_viewport_set_source(win->viewport,
                wl_fixed_from_double(100.0),   // src_x
                wl_fixed_from_double(100.0),   // src_y
                wl_fixed_from_double(200.0),   // src_width
                wl_fixed_from_double(200.0)    // src_height
            );

            // 目标尺寸: 将裁剪后的 200x200 缩放到 300x300
            wp_viewport_set_destination(win->viewport, 300, 300);

            printf("Viewport set: source (100,100) 200x200 -> destination 300x300\n");
            printf("Buffer is 400x400, but surface displays as 300x300\n");
        }

        wl_surface_commit(win->surface);
        win->is_configured = true;
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct ClientState *state = data;
    state->running = false;
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// ---------------------------------------------------------
// 全局注册表处理
// ---------------------------------------------------------
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = { .ping = xdg_wm_base_ping };

static void registry_handler(void *data, struct wl_registry *registry, uint32_t id, const char *interface, uint32_t version) {
    struct ClientState *state = data;

    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, id, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, id, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(registry, id, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, wp_viewporter_interface.name) == 0) {
        // 绑定 viewporter 全局对象
        state->viewporter = wl_registry_bind(registry, id, &wp_viewporter_interface, 1);
        printf("Found wp_viewporter\n");
    }
}

static void registry_remover(void *data, struct wl_registry *registry, uint32_t id) {}
static const struct wl_registry_listener registry_listener = {
    .global = registry_handler,
    .global_remove = registry_remover
};

// ---------------------------------------------------------
// 主函数
// ---------------------------------------------------------
int main() {
    struct ClientState state = {0};
    state.running = true;

    // 1. 连接 Wayland 显示服务器
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    // 2. 获取全局对象
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display);

    // 3. 检查必要的协议支持
    if (!state.viewporter) {
        fprintf(stderr, "Compositor does not support wp_viewporter protocol.\n");
        fprintf(stderr, "This demo requires a compositor with viewporter support.\n");
        return -1;
    }

    // 4. 创建窗口
    struct Window *win = calloc(1, sizeof(struct Window));
    win->state = &state;
    win->width = 300;
    win->height = 300;
    win->is_configured = false;

    win->surface = wl_compositor_create_surface(state.compositor);

    // 创建 viewport 对象，关联到 surface
    win->viewport = wp_viewporter_get_viewport(state.viewporter, win->surface);

    win->xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);

    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(win->xdg_toplevel, "Viewporter Demo");

    wl_surface_commit(win->surface);

    printf("Viewporter Demo\n");
    printf("===============\n");
    printf("Buffer size: 400x400 (checkerboard pattern)\n");
    printf("Source rectangle: (100,100) 200x200 (cropped)\n");
    printf("Destination size: 300x300 (scaled up)\n");
    printf("The red square should appear at the center.\n\n");

    // 5. 主事件循环
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // 等待事件
    }

    // 6. 清理资源
    if (win->viewport) wp_viewport_destroy(win->viewport);
    xdg_toplevel_destroy(win->xdg_toplevel);
    xdg_surface_destroy(win->xdg_surface);
    wl_surface_destroy(win->surface);
    free(win);

    wp_viewporter_destroy(state.viewporter);
    xdg_wm_base_destroy(state.xdg_wm_base);
    wl_shm_destroy(state.shm);
    wl_compositor_destroy(state.compositor);
    wl_registry_destroy(state.registry);
    wl_display_disconnect(state.display);

    return 0;
}
