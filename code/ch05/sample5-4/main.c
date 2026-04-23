#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include <wayland-cursor.h>
#include "xdg-shell-client-protocol.h"

// ---------------------------------------------------------
// 1. 全局状态与数据结构
// ---------------------------------------------------------
struct ClientState {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;
    struct wl_surface *cursor_surface;
    struct wl_cursor_image *cursor_image;
};

struct Window {
    struct ClientState *state;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;
    int width, height;
    uint32_t color;
    bool is_configured;
};

// ---------------------------------------------------------
// 2. 共享内存 (SHM) 绘图辅助函数
// ---------------------------------------------------------
static struct wl_buffer *create_shm_buffer(struct wl_shm *shm, int width, int height, uint32_t color) {
    int stride = width * 4; // ARGB8888, 4 bytes per pixel
    int size = stride * height;

    // 创建一个匿名内存文件 (Linux 特有)
    int fd = memfd_create("wayland-shm-buffer", MFD_CLOEXEC);
    ftruncate(fd, size);

    // 映射内存并填充颜色
    uint32_t *pixels = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < width * height; ++i) {
        pixels[i] = color;
    }
    munmap(pixels, size);

    // 将内存文件交给 Wayland Compositor
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    
    wl_shm_pool_destroy(pool);
    close(fd);
    return buffer;
}

// ---------------------------------------------------------
// 3. XDG Surface 事件处理 (极其重要)
// ---------------------------------------------------------
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct Window *win = data;
    
    // 必须回复 ack_configure，否则 Compositor 会认为窗口卡死
    xdg_surface_ack_configure(xdg_surface, serial);

    // 如果是第一次配置，分配内存、附加并提交（渲染画面）
    if (!win->is_configured) {
        struct wl_buffer *buffer = create_shm_buffer(win->state->shm, win->width, win->height, win->color);
        wl_surface_attach(win->surface, buffer, 0, 0);
        wl_surface_commit(win->surface);
        win->is_configured = true;
        printf("Window is now displayed.\n");
    }
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *states) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) { exit(0); }
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// ---------------------------------------------------------
// 4. 鼠标指针处理
// ---------------------------------------------------------
static void pointer_enter(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y) {
    struct ClientState *state = data;
    wl_pointer_set_cursor(pointer, serial, state->cursor_surface,
                          state->cursor_image->hotspot_x, state->cursor_image->hotspot_y);
}

static void pointer_leave(void *data, struct wl_pointer *pointer, uint32_t serial,
                          struct wl_surface *surface) {}

static void pointer_motion(void *data, struct wl_pointer *pointer, uint32_t time,
                           wl_fixed_t x, wl_fixed_t y) {}

static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial,
                           uint32_t time, uint32_t button, uint32_t state) {}

static void pointer_axis(void *data, struct wl_pointer *pointer, uint32_t time,
                         uint32_t axis, wl_fixed_t value) {}

static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter,
    .leave = pointer_leave,
    .motion = pointer_motion,
    .button = pointer_button,
    .axis = pointer_axis,
};

// ---------------------------------------------------------
// 5. Seat 处理
// ---------------------------------------------------------
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct ClientState *state = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
        state->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
    }
}

static void seat_name(void *data, struct wl_seat *seat, const char *name) {}

static const struct wl_seat_listener seat_listener = {
    .capabilities = seat_capabilities,
    .name = seat_name,
};

// ---------------------------------------------------------
// 6. 全局注册表处理
// ---------------------------------------------------------
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial); // 心跳保活
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
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = wl_registry_bind(registry, id, &wl_seat_interface, 1);
        wl_seat_add_listener(state->seat, &seat_listener, state);
    }
}
static const struct wl_registry_listener registry_listener = { .global = registry_handler };

// ---------------------------------------------------------
// 7. 辅助函数：创建窗口
// ---------------------------------------------------------
struct Window* create_window(struct ClientState *state, int width, int height, uint32_t color, const char *title) {
    struct Window *win = calloc(1, sizeof(struct Window));
    win->state = state;
    win->width = width;
    win->height = height;
    win->color = color;
    win->is_configured = false;

    // 1. 创建基础 wl_surface
    win->surface = wl_compositor_create_surface(state->compositor);
    
    // 2. 赋予 xdg_surface 角色
    win->xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, win->surface);
    xdg_surface_add_listener(win->xdg_surface, &xdg_surface_listener, win);
    
    // 3. 赋予 toplevel 桌面窗口角色
    win->xdg_toplevel = xdg_surface_get_toplevel(win->xdg_surface);
    xdg_toplevel_add_listener(win->xdg_toplevel, &xdg_toplevel_listener, win);
    xdg_toplevel_set_title(win->xdg_toplevel, title);

    // 先 commit 提交初始状态，Compositor 才会下发 configure 事件
    wl_surface_commit(win->surface);
    return win;
}

// ---------------------------------------------------------
// Main
// ---------------------------------------------------------
int main() {
    struct ClientState state = {0};

    // 1. 连接 Wayland 显示服务器
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to connect to Wayland display.\n");
        return -1;
    }

    // 2. 获取全局对象
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display); // 等待所有全局对象绑定完毕
    wl_display_roundtrip(state.display); // 等待 seat capabilities

    if (!state.compositor || !state.shm || !state.xdg_wm_base || !state.seat) {
        fprintf(stderr, "Missing required Wayland interfaces.\n");
        return -1;
    }

    // 3. 初始化光标
    struct wl_cursor_theme *cursor_theme = wl_cursor_theme_load(NULL, 24, state.shm);
    struct wl_cursor *cursor = wl_cursor_theme_get_cursor(cursor_theme, "left_ptr");
    state.cursor_image = cursor->images[0];
    struct wl_buffer *cursor_buffer = wl_cursor_image_get_buffer(state.cursor_image);

    state.cursor_surface = wl_compositor_create_surface(state.compositor);
    wl_surface_attach(state.cursor_surface, cursor_buffer, 0, 0);
    wl_surface_commit(state.cursor_surface);

    // 4. 创建主窗口 (蓝色背景, 800x600)
    struct Window *main_window = create_window(&state, 800, 600, 0xFF0000FF, "Main Window");

    // 5. 等待主窗口配置完成（显示出来）
    printf("Waiting for main window to be configured...\n");
    while (!main_window->is_configured && wl_display_dispatch(state.display) != -1) {
        // 等待主窗口收到 configure 事件并完成渲染
    }
    printf("Main window is now displayed.\n");

    // 6. 创建第二个窗口 (红色背景, 400x300)
    struct Window *popup_window = create_window(&state, 400, 300, 0xFFFF0000, "Popup Window");

    // ★ 7. 核心逻辑：设置第二个窗口的 Parent 为主窗口 ★
    // 注意：必须在 popup_window mapped 之前设置 parent，然后 commit 生效
    xdg_toplevel_set_parent(popup_window->xdg_toplevel, main_window->xdg_toplevel);

    // 8. 主事件循环：在这里挂起，持续处理 Wayland 事件
    printf("Windows created. Try Alt+Tab or clicking away and back.\n");
    while (wl_display_dispatch(state.display) != -1) {
        // 这个循环同时服务于两个窗口！
        // 当你切回应用时，Wayland 会向两个窗口发送 configure
        // 由于循环未被阻塞，主窗口依然能处理它自己的 xdg_surface_configure 进行绘制和保活。
    }

    // 清理
    wl_cursor_theme_destroy(cursor_theme);
    wl_surface_destroy(state.cursor_surface);
    if (state.pointer) wl_pointer_destroy(state.pointer);
    wl_seat_destroy(state.seat);
    wl_display_disconnect(state.display);
    return 0;
}
