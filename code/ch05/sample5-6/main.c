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
#include "xdg-activation-v1-client-protocol.h"

// 简单的共享内存创建函数
static int create_shm_file(off_t size) {
    int fd = memfd_create("wl_shm_buffer", MFD_CLOEXEC);
    if (fd < 0) return -1;
    if (ftruncate(fd, size) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

struct Window {
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    int width, height;
    uint32_t color;
    struct wl_buffer *buffer;
};

struct AppState {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_shm *shm;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct xdg_activation_v1 *activation;

    struct Window *main_window;
    struct Window *tool_window;

    // 鼠标状态追踪
    struct wl_surface *pointer_surface;
    int pointer_x, pointer_y;
};

// --- XDG 激活回调 ---
static void activation_token_done(void *data, struct xdg_activation_token_v1 *token, const char *token_str) {
    struct AppState *app = data;
    printf("获得 Activation Token: %s，正在激活 Main Window...\n", token_str);
    
    // 使用拿到的 Token 激活主窗口
    xdg_activation_v1_activate(app->activation, token_str, app->main_window->surface);
    xdg_activation_token_v1_destroy(token);
}

static const struct xdg_activation_token_v1_listener activation_token_listener = {
    .done = activation_token_done,
};

// --- 输入事件处理 (鼠标) ---
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct AppState *app = data;
    app->pointer_surface = surface;
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial, struct wl_surface *surface) {
    struct AppState *app = data;
    app->pointer_surface = NULL;
}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time, wl_fixed_t surface_x, wl_fixed_t surface_y) {
    struct AppState *app = data;
    app->pointer_x = wl_fixed_to_int(surface_x);
    app->pointer_y = wl_fixed_to_int(surface_y);
}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state) {
    struct AppState *app = data;

    // 当鼠标左键按下时 (BTN_LEFT = 272, WL_POINTER_BUTTON_STATE_PRESSED = 1)
    if (button == 272 && state == WL_POINTER_BUTTON_STATE_PRESSED) {
        // 如果当前在 Tool Window 上
        if (app->pointer_surface == app->tool_window->surface) {
            // 检查是否点在了 "红色按钮" 区域内 (假设在坐标中心 50x50 的区域)
            int bx = (app->tool_window->width - 50) / 2;
            int by = (app->tool_window->height - 50) / 2;
            
            if (app->pointer_x >= bx && app->pointer_x <= bx + 50 &&
                app->pointer_y >= by && app->pointer_y <= by + 50) {
                
                printf("点击了 Tool Window 的按钮！申请激活主窗口...\n");

                if (!app->activation) {
                    printf("混成器不支持 xdg_activation_v1 协议！\n");
                    return;
                }

                // 1. 获取 token 对象
                struct xdg_activation_token_v1 *token = xdg_activation_v1_get_activation_token(app->activation);
                
                // 2. 设置必要的触发信息 (关键所在：必须附带本次点击事件的 serial 和所在 seat)
                xdg_activation_token_v1_set_serial(token, serial, app->seat);
                xdg_activation_token_v1_set_surface(token, app->tool_window->surface);
                
                // 3. 监听 token 的回调并提交请求
                xdg_activation_token_v1_add_listener(token, &activation_token_listener, app);
                xdg_activation_token_v1_commit(token);
            }
        }
    }
}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time, uint32_t axis, wl_fixed_t value) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t capabilities) {
    struct AppState *app = data;
    if (capabilities & WL_SEAT_CAPABILITY_POINTER) {
        app->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(app->pointer, &pointer_listener, app);
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_capabilities, .name = seat_name };

// --- XDG Shell 基础回调 ---
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = { .ping = xdg_wm_base_ping };

// --- 窗口渲染与创建 ---
static struct wl_buffer *create_buffer(struct wl_shm *shm, int width, int height, uint32_t bg_color, bool is_tool) {
    int stride = width * 4;
    int size = stride * height;
    int fd = create_shm_file(size);
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    
    // 填充背景色
    for (int i = 0; i < width * height; ++i) pixels[i] = bg_color;

    // 如果是 Tool Window，我们在中间画一个 50x50 的红框作为“按钮”
    if (is_tool) {
        int bx = (width - 50) / 2;
        int by = (height - 50) / 2;
        for (int y = by; y < by + 50; y++) {
            for (int x = bx; x < bx + 50; x++) {
                pixels[y * width + x] = 0xFFFF0000; // 红色 AARRGGBB
            }
        }
    }

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    
    wl_shm_pool_destroy(pool);
    close(fd);
    munmap(pixels, size);
    return buffer;
}

static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct Window *win = data;
    xdg_surface_ack_configure(xdg_surface, serial);
    wl_surface_attach(win->surface, win->buffer, 0, 0);
    wl_surface_commit(win->surface);
}
static const struct xdg_surface_listener xdg_surface_listener = { .configure = xdg_surface_configure };

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) { exit(0); }
static const struct xdg_toplevel_listener xdg_toplevel_listener = { .configure = xdg_toplevel_configure, .close = xdg_toplevel_close };

static struct Window* create_window(struct AppState *app, int width, int height, uint32_t color, const char *title, bool is_tool) {
    struct Window *win = calloc(1, sizeof(struct Window));
    win->width = width; win->height = height; win->color = color;
    win->buffer = create_buffer(app->shm, width, height, color, is_tool);
    win->surface = wl_compositor_create_surface(app->compositor);
    win->xdg_surface = xdg_wm_base_get_xdg_surface(app->xdg_wm_base, win->surface);
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);

    xdg_toplevel_set_title(win->xdg_toplevel, title);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);

    wl_surface_commit(win->surface);
    return win;
}

// --- Registry 处理 ---
static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct AppState *app = data;
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        app->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        app->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        app->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(app->xdg_wm_base, &xdg_wm_base_listener, app);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        app->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(app->seat, &seat_listener, app);
    } else if (strcmp(interface, xdg_activation_v1_interface.name) == 0) {
        // 绑定 xdg_activation_v1 协议
        app->activation = wl_registry_bind(registry, name, &xdg_activation_v1_interface, 1);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { .global = registry_global, .global_remove = registry_global_remove };

int main(int argc, char **argv) {
    struct AppState app = {0};
    app.display = wl_display_connect(NULL);
    if (!app.display) return -1;

    app.registry = wl_display_get_registry(app.display);
    wl_registry_add_listener(app.registry, &registry_listener, &app);
    wl_display_roundtrip(app.display); // 等待 globals 绑定完成

    if (!app.compositor || !app.shm || !app.xdg_wm_base) {
        fprintf(stderr, "缺少必要的 Wayland 接口\n");
        return -1;
    }

    // 1. 创建 Main Window (绿色背景，主窗口)
    app.main_window = create_window(&app, 400, 300, 0xFF00FF00, "Main Window", false);

    // 2. 创建 Tool Window (灰色背景，中间带个红方块作按钮)
    app.tool_window = create_window(&app, 200, 150, 0xFF888888, "Tool Window", true);

    wl_display_roundtrip(app.display);

    printf("程序已启动。\n请点击 Tool Window 中间的红色按钮，以激活并前置 Main Window。\n");

    // 事件循环
    while (wl_display_dispatch(app.display) != -1) {
        // Wait for events
    }

    wl_display_disconnect(app.display);
    return 0;
}
