#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include <linux/input-event-codes.h>

// 全局客户端状态
struct client_state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *xdg_wm_base;
    struct wl_seat *seat;
    struct wl_pointer *pointer;

    // 主窗口
    struct wl_surface *main_surface;
    struct xdg_surface *main_xdg_surface;
    struct xdg_toplevel *main_toplevel;
    struct wl_buffer *main_buffer;

    // 弹出菜单窗口
    struct wl_surface *popup_surface;
    struct xdg_surface *popup_xdg_surface;
    struct xdg_popup *popup;
    struct wl_buffer *popup_buffer;
    struct wl_surface *pointer_surface;  // 记录当前鼠标指针所在的 surface

    // 鼠标坐标状态
    int ptr_x, ptr_y;
    bool running;
};

// --- 辅助函数：通过 Shared Memory (shm) 创建渲染 Buffer ---
static struct wl_buffer *create_buffer(struct client_state *state, int width, int height, uint32_t color) {
    int stride = width * 4;
    int size = stride * height;
    
    int fd = memfd_create("shm_buffer", MFD_CLOEXEC);
    ftruncate(fd, size);
    
    // 映射内存并填充颜色
    uint32_t *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    for (int i = 0; i < width * height; i++) {
        data[i] = color;
    }
    munmap(data, size);

    // 将内存提交给 Wayland 合成器
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool, 0, width, height, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    
    return buffer;
}

static void destroy_popup(struct client_state *state) {
    if (state->popup) {
        xdg_popup_destroy(state->popup);
        xdg_surface_destroy(state->popup_xdg_surface);
        wl_surface_destroy(state->popup_surface);
        state->popup = NULL;
        state->popup_xdg_surface = NULL;
        state->popup_surface = NULL;
        printf("Popup dismissed.\n");
    }
}

// --- XDG Popup 监听器 ---
static void xdg_popup_configure(void *data, struct xdg_popup *popup, int32_t x, int32_t y, int32_t w, int32_t h) {}
static void xdg_popup_popup_done(void *data, struct xdg_popup *popup) {
    // 当用户点击菜单外部时触发，需要我们手动销毁弹出窗口对象
    struct client_state *state = data;
    printf("Popup dismissed (clicked outside application)\n");
    destroy_popup(state);
}
static const struct xdg_popup_listener xdg_popup_listener = {
    .configure = xdg_popup_configure,
    .popup_done = xdg_popup_popup_done,
};

// --- XDG Surface 监听器 (窗口初次呈现) ---
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct client_state *state = data;
    xdg_surface_ack_configure(xdg_surface, serial); // 必须回应 ACK

    // 为主窗口贴图
    if (xdg_surface == state->main_xdg_surface) {
        if (!state->main_buffer) {
            state->main_buffer = create_buffer(state, 640, 480, 0xFF336699); // 蓝灰色
        }
        wl_surface_attach(state->main_surface, state->main_buffer, 0, 0);
        wl_surface_damage(state->main_surface, 0, 0, 640, 480);
        wl_surface_commit(state->main_surface);
    } 
    // 为弹出菜单贴图
    else if (xdg_surface == state->popup_xdg_surface) {
        if (!state->popup_buffer) {
            state->popup_buffer = create_buffer(state, 150, 200, 0xFFDD5555); // 红色
        }
        wl_surface_attach(state->popup_surface, state->popup_buffer, 0, 0);
        wl_surface_damage(state->popup_surface, 0, 0, 150, 200);
        wl_surface_commit(state->popup_surface);
    }
}
static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure,
};

// --- XDG Toplevel 监听器 (主窗口行为) ---
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *toplevel, int32_t w, int32_t h, struct wl_array *s) {}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *toplevel) {
    struct client_state *state = data;
    state->running = false; // 用户点击了主窗口的 X 关闭按钮
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
};

// --- 鼠标行为监听器 ---
static void pointer_enter(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf, wl_fixed_t x, wl_fixed_t y)
{
    struct client_state *state = d;
    state->pointer_surface = sf; // 记录鼠标进入了哪个表面
    state->ptr_x = wl_fixed_to_int(x);
    state->ptr_y = wl_fixed_to_int(y);
}

static void pointer_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf)
{
    struct client_state *state = d;
    if (state->pointer_surface == sf) {
        state->pointer_surface = NULL; // 鼠标离开
    }
}

static void pointer_motion(void *d, struct wl_pointer *p, uint32_t t, wl_fixed_t x, wl_fixed_t y) {
    struct client_state *state = d;
    // 持续追踪鼠标在表面上的坐标，为了在点击时知道菜单显示在哪里
    state->ptr_x = wl_fixed_to_int(x);
    state->ptr_y = wl_fixed_to_int(y);
}
static void pointer_button(void *data, struct wl_pointer *pointer, uint32_t serial, uint32_t time, uint32_t button, uint32_t state_action) {
    struct client_state *state = data;

    // 当用户按下鼠标右键
    if (state_action == WL_POINTER_BUTTON_STATE_PRESSED) {
        // 逻辑 1：如果菜单已经处于打开状态
        if (state->popup) {
            // 如果点击发生的 surface 不是弹出菜单自身（说明点在了主窗口上）
            if (state->pointer_surface != state->popup_surface) {
                printf("Clicked on parent window, closing popup.\n");
                destroy_popup(state); // 手动关闭菜单
                // 注意：这里我们不 return，继续往下走
                // 这样如果用户是在主窗口其他地方重新按了右键，旧的会关闭，新的会接着弹出来（符合直觉）
            } else {
                // 点击在菜单内部（比如点击菜单项）
                printf("Clicked INSIDE popup menu.\n");
                return; 
            }
        }

        // 逻辑 2：当按下鼠标右键，且此时没有打开的菜单时，创建菜单
        if (!state->popup && button == BTN_RIGHT) {
            printf("Right clicked at (%d, %d), spawning popup.\n", state->ptr_x, state->ptr_y);

            // 1. 创建 popup 表面
            state->popup_surface = wl_compositor_create_surface(state->compositor);
            state->popup_xdg_surface = xdg_wm_base_get_xdg_surface(state->xdg_wm_base, state->popup_surface);
            xdg_surface_add_listener(state->popup_xdg_surface, &xdg_surface_listener, state);

            // 2. 使用 Positioner 决定 Popup 生成位置（定锚在鼠标处）
            struct xdg_positioner *pos = xdg_wm_base_create_positioner(state->xdg_wm_base);
            xdg_positioner_set_size(pos, 150, 200);
            xdg_positioner_set_anchor_rect(pos, state->ptr_x, state->ptr_y, 1, 1);
            xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT); // 从鼠标右下方弹开
            xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
            
            // 允许靠屏幕边缘时自动滑动反转
            xdg_positioner_set_constraint_adjustment(pos, 
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_X | 
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_SLIDE_Y |
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_X |
                XDG_POSITIONER_CONSTRAINT_ADJUSTMENT_FLIP_Y);

            // 3. 分配角色：以主窗口为父节点，实例化 popup
            state->popup = xdg_surface_get_popup(state->popup_xdg_surface, state->main_xdg_surface, pos);
            xdg_positioner_destroy(pos);
            
            xdg_popup_add_listener(state->popup, &xdg_popup_listener, state);
            
            // 4. 获取 Grab（重要）：捕获后续输入，实现点击其它地方消失的功能
            xdg_popup_grab(state->popup, state->seat, serial);

            // 5. 初始 commit，触发第一轮 configure 从而为 Popup 分配缓冲区渲染
            wl_surface_commit(state->popup_surface);
        }
    }
}
static void pointer_axis(void *d, struct wl_pointer *p, uint32_t t, uint32_t a, wl_fixed_t v) {}
static const struct wl_pointer_listener pointer_listener = {
    .enter = pointer_enter, .leave = pointer_leave, 
    .motion = pointer_motion, .button = pointer_button, .axis = pointer_axis
};

// --- Seat 监听器 ---
static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps) {
    struct client_state *state = data;
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !state->pointer) {
        state->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(state->pointer, &pointer_listener, state);
    }
}
static void seat_name(void *data, struct wl_seat *seat, const char *name) {}
static const struct wl_seat_listener seat_listener = { .capabilities = seat_capabilities, .name = seat_name };

// --- 基础服务监听器 ---
static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    xdg_wm_base_pong(xdg_wm_base, serial); // 回应合成器的存活检测
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = { .ping = xdg_wm_base_ping };

static void registry_global(void *data, struct wl_registry *registry, uint32_t name, const char *interface, uint32_t version) {
    struct client_state *state = data;
    // 强制使用 version 1 即可满足大部分基本需求
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 1);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, wl_seat_interface.name) == 0) {
        state->seat = wl_registry_bind(registry, name, &wl_seat_interface, 1);
        wl_seat_add_listener(state->seat, &seat_listener, state);
    }
}
static void registry_global_remove(void *data, struct wl_registry *registry, uint32_t name) {}
static const struct wl_registry_listener registry_listener = { .global = registry_global, .global_remove = registry_global_remove };


int main() {
    struct client_state state = {0};
    state.running = true;

    // 连接显示服务器
    state.display = wl_display_connect(NULL);
    if (!state.display) {
        fprintf(stderr, "Failed to connect to Wayland display\n");
        return 1;
    }

    // 注册并获取基础全局对象
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
    wl_display_roundtrip(state.display); // 等待 globals
    wl_display_roundtrip(state.display); // 等待 seat capabilities

    if (!state.compositor || !state.shm || !state.xdg_wm_base || !state.seat) {
        fprintf(stderr, "Missing required Wayland interfaces\n");
        return 1;
    }

    // 创建主窗口 (Toplevel)
    state.main_surface = wl_compositor_create_surface(state.compositor);
    state.main_xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.main_surface);
    xdg_surface_add_listener(state.main_xdg_surface, &xdg_surface_listener, &state);

    state.main_toplevel = xdg_surface_get_toplevel(state.main_xdg_surface);
    xdg_toplevel_add_listener(state.main_toplevel, &xdg_toplevel_listener, &state);
    xdg_toplevel_set_title(state.main_toplevel, "Wayland Popup Demo");

    wl_surface_commit(state.main_surface); // 触发初次 configure

    printf("Window created! Right-click anywhere inside the window to show the popup.\n");

    // 主事件循环
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // 事件交由内部处理
    }

    // 释放资源
    if (state.popup) xdg_popup_destroy(state.popup);
    if (state.popup_xdg_surface) xdg_surface_destroy(state.popup_xdg_surface);
    if (state.popup_surface) wl_surface_destroy(state.popup_surface);

    if (state.main_buffer) wl_buffer_destroy(state.main_buffer);
    if (state.popup_buffer) wl_buffer_destroy(state.popup_buffer);

    xdg_toplevel_destroy(state.main_toplevel);
    xdg_surface_destroy(state.main_xdg_surface);
    wl_surface_destroy(state.main_surface);

    if (state.pointer) wl_pointer_destroy(state.pointer);
    wl_seat_destroy(state.seat);
    xdg_wm_base_destroy(state.xdg_wm_base);
    wl_shm_destroy(state.shm);
    wl_compositor_destroy(state.compositor);
    wl_registry_destroy(state.registry);
    wl_display_disconnect(state.display);

    return 0;
}
