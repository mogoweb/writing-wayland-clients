#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wayland-client.h>
#include <wayland-client-protocol.h>
#include <cairo/cairo.h>
#include <sys/mman.h>
#include <unistd.h>
#include <fcntl.h>

#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-client-protocol.h"
#include "treeland-foreign-toplevel-manager.h"

// 每个窗口对应的私有数据
struct foreign_window {
    struct wl_list link;    /* 链表节点：用于挂载到 state->toplevel_list */
    struct state *app_state; /* 反向引用，方便在回调中访问全局状态 */
    struct treeland_foreign_toplevel_handle_v1 *handle;
    uint32_t pid;
    char *title;
    char *app_id;
    uint32_t identifier;
    _Bool is_maximized;
    _Bool is_minimized;
    _Bool is_activated;
    _Bool is_fullscreen;
};

// 用于管理我们所有Wayland对象和状态的结构体
struct state {
    struct wl_display *display;
    struct wl_registry *registry;
    struct wl_compositor *compositor;
    struct wl_surface *surface;
    struct wl_shm *shm;

    struct xdg_wm_base *xdg_wm_base;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *xdg_toplevel;

    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct zxdg_toplevel_decoration_v1 *toplevel_decoration;

    struct treeland_foreign_toplevel_manager_v1 *foreign_toplevel_manager;
    struct treeland_foreign_toplevel_handle_v1 *my_toplevel_handle;
    struct wl_list toplevel_list; 
 
    struct wl_buffer *buffer;
    void *shm_data;
    int pool_size;

    int width, height;
    _Bool running;
};

static void destroy_shm_buffer(struct state *state) {
    if (state->buffer) {
        wl_buffer_destroy(state->buffer);
        state->buffer = NULL;
    }
    if (state->shm_data) {
        munmap(state->shm_data, state->pool_size);
        state->shm_data = NULL;
    }
}

// 创建共享内存缓冲区
static int create_shm_buffer(struct state *state) {
    destroy_shm_buffer(state);

    // 使用 memfd_create 创建一个匿名的、基于内存的文件
    char tmp_name[] = "/tmp/wayland-shm-XXXXXX";
    int fd = mkstemp(tmp_name);
    if (fd < 0) {
        fprintf(stderr, "mkstemp failed\n");
        return -1;
    }
    // 立即删除文件名，文件描述符依然有效
    unlink(tmp_name);
 
    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, state->width);
    int size = stride * state->height;
    state->pool_size = size;
 
    if (ftruncate(fd, size) < 0) {
        close(fd);
        fprintf(stderr, "ftruncate failed\n");
        return -1;
    }
 
    // 将文件映射到内存
    state->shm_data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (state->shm_data == MAP_FAILED) {
        close(fd);
        fprintf(stderr, "mmap failed\n");
        return -1;
    }
 
    // 从文件描述符创建Wayland共享内存池
    struct wl_shm_pool *pool = wl_shm_create_pool(state->shm, fd, size);
    state->buffer = wl_shm_pool_create_buffer(pool, 0, state->width, state->height, stride, WL_SHM_FORMAT_ARGB8888);
    
    wl_shm_pool_destroy(pool);
    close(fd);
    return 0;
}
 
// 绘制函数
static void draw_frame(struct state *state) {
    if (create_shm_buffer(state) < 0) return;

    int stride = cairo_format_stride_for_width(CAIRO_FORMAT_ARGB32, state->width);
    int size = stride * state->height;
    printf("Drawing frame with size %dx%d\n", state->width, state->height);
 
    // 清空缓冲区内存
    memset(state->shm_data, 0, size);
    printf("Clearing buffer memory\n");
 
    // 使用Cairo在共享内存上创建表面
    cairo_surface_t *cairo_surface = cairo_image_surface_create_for_data(
        state->shm_data, CAIRO_FORMAT_ARGB32, state->width, state->height, stride);
    cairo_t *cr = cairo_create(cairo_surface);
 
    // 绘制背景 (淡蓝色)
    cairo_set_source_rgba(cr, 0.8, 0.9, 1.0, 1.0);
    cairo_paint(cr);
 
    // 绘制文字
    cairo_set_source_rgb(cr, 0.1, 0.1, 0.1);
    cairo_select_font_face(cr, "sans-serif", CAIRO_FONT_SLANT_NORMAL, CAIRO_FONT_WEIGHT_BOLD);
    cairo_set_font_size(cr, 40);
 
    cairo_text_extents_t extents;
    cairo_text_extents(cr, "Hello World!", &extents);
    cairo_move_to(cr, state->width/2.0 - extents.width/2.0, state->height/2.0);
    cairo_show_text(cr, "Hello World!");
 
    // 清理Cairo资源
    cairo_destroy(cr);
    cairo_surface_destroy(cairo_surface);
 
    // 将绘制好的缓冲区附加到表面
    wl_surface_attach(state->surface, state->buffer, 0, 0);
    // 告诉合成器表面的哪个区域被更新了 (这里是整个表面)
    wl_surface_damage_buffer(state->surface, 0, 0, state->width, state->height);
    // 提交更改，让合成器显示
    wl_surface_commit(state->surface);
}

static void xdg_toplevel_handle_configure(void *data, struct xdg_toplevel *xdg_toplevel,
                                          int32_t width, int32_t height, struct wl_array *states) {
    struct state *state = data;
    if (width > 0 && height > 0) {
        state->width = width;
        state->height = height;

        // 创建共享内存缓冲区用于绘图
        if (create_shm_buffer(state) < 0) {
            fprintf(stderr, "Failed to create shm buffer\n");
            return;
        }
    }
    // 注意: 我们不在这里绘图，因为我们会在xdg_surface的configure事件后绘图
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct state *state = data;
    // 合成器通知我们用户点击了关闭按钮
    state->running = 0;
    printf("User requested close\n");
}

static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_handle_configure,
    .close = xdg_toplevel_handle_close,
};

// --- xdg_surface 事件监听器 ---
static void xdg_surface_handle_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    struct state *state = data;
    // 必须确认配置事件
    xdg_surface_ack_configure(xdg_surface, serial);
    // 在收到配置后，我们就可以绘图了
    draw_frame(state);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_handle_configure,
};

// --- xdg_wm_base 事件监听器 ---
static void xdg_wm_base_handle_ping(void *data, struct xdg_wm_base *xdg_wm_base, uint32_t serial) {
    // 客户端必须响应ping事件，否则合成器会认为客户端无响应
    xdg_wm_base_pong(xdg_wm_base, serial);
}

static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    .ping = xdg_wm_base_handle_ping,
};

// 1. PID 变化
static void handle_pid(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, uint32_t pid) {
    struct foreign_window *win = data;
    win->pid = pid;
    printf("[Window %p] PID: %u\n", (void*)handle, pid);
    if (pid == getpid()) {
        printf("识别成功：这就是我自己的窗口句柄！\n");
        win->app_state->my_toplevel_handle = handle; // 保存起来
    }
}

// 2. 标题变化
static void handle_title(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, const char *title) {
    struct foreign_window *win = data;
    free(win->title);
    win->title = strdup(title);
    printf("[Window %p] Title: %s\n", (void*)handle, title);
}

// 3. App ID 变化 (通常用于匹配图标)
static void handle_app_id(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, const char *app_id) {
    struct foreign_window *win = data;
    free(win->app_id);
    win->app_id = strdup(app_id);
    printf("[Window %p] App ID: %s\n", (void*)handle, app_id);
}

// 4. 唯一标识符
static void handle_identifier(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, uint32_t identifier) {
    struct foreign_window *win = data;
    win->identifier = identifier;
    printf("[Window %p] Identifier: %u\n", (void*)handle, identifier);
}

// 5. 进入输出设备 (屏幕)
static void handle_output_enter(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    printf("[Window %p] Entered output %p\n", (void*)handle, (void*)output);
}

// 6. 离开输出设备
static void handle_output_leave(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, struct wl_output *output) {
    printf("[Window %p] Left output %p\n", (void*)handle, (void*)output);
}

// 7. 状态变化 (核心：判断最小化/最大化/激活)
static void handle_state(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, struct wl_array *states) {
    struct foreign_window *win = data;
    win->is_maximized = 0;
    win->is_minimized = 0;
    win->is_activated = 0;
    win->is_fullscreen = 0;

    // 只处理属于我自己的窗口
    if (handle != win->app_state->my_toplevel_handle) return;

    uint32_t *state;
    wl_array_for_each(state, states) {
        switch (*state) {
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MAXIMIZED:
                win->is_maximized = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_MINIMIZED:
                win->is_minimized = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_ACTIVATED:
                win->is_activated = 1;
                break;
            case TREELAND_FOREIGN_TOPLEVEL_HANDLE_V1_STATE_FULLSCREEN:
                win->is_fullscreen = 1;
                break;
            default:
                // 忽略未来协议可能扩展的其他状态
                break;
        }
    }
    printf("[Window %p] States: [ %s %s %s %s]\n", (void*)handle,
           win->is_activated ? "ACTIVATED" : "",
           win->is_minimized ? "MINIMIZED" : "",
           win->is_maximized ? "MAXIMIZED" : "",
           win->is_fullscreen ? "FULLSCREEN" : "");
}

// 8. 数据发送完成 (原子更新点)
static void handle_done(void *data, struct treeland_foreign_toplevel_handle_v1 *handle) {
    // 此时所有属性都已同步，可以通知 UI 更新了
    printf("[Window %p] All properties synced.\n", (void*)handle);
}

// 9. 窗口关闭 (清理资源)
static void handle_closed(void *data, struct treeland_foreign_toplevel_handle_v1 *handle) {
    struct foreign_window *win = data;
    printf("[Window %p] Closed. Cleaning up...\n", (void*)handle);

    // 1. 从链表中移除
    wl_list_remove(&win->link);
    
    free(win->title);
    free(win->app_id);

    treeland_foreign_toplevel_handle_v1_destroy(win->handle);
    free(win);
}

// 10. 父窗口变化
static void handle_parent(void *data, struct treeland_foreign_toplevel_handle_v1 *handle, struct treeland_foreign_toplevel_handle_v1 *parent) {
    printf("[Window %p] Parent is now %p\n", (void*)handle, (void*)parent);
}

// 组装 Listener
static const struct treeland_foreign_toplevel_handle_v1_listener handle_listener = {
    .pid = handle_pid,
    .title = handle_title,
    .app_id = handle_app_id,
    .identifier = handle_identifier,
    .output_enter = handle_output_enter,
    .output_leave = handle_output_leave,
    .state = handle_state,
    .done = handle_done,
    .closed = handle_closed,
    .parent = handle_parent,
};

static void foreign_toplevel_manager_handle_toplevel(void *data,
             struct treeland_foreign_toplevel_manager_v1 *manager,
             struct treeland_foreign_toplevel_handle_v1 *handle) {
    struct state *state = data;
    printf("发现新窗口！句柄指针: %p\n", (void *)handle);

    struct foreign_window *win = calloc(1, sizeof(struct foreign_window));
    win->handle = handle;
    win->app_state = state;

    // 将新窗口插入到 state 的链表中
    wl_list_insert(&state->toplevel_list, &win->link);

    treeland_foreign_toplevel_handle_v1_add_listener(handle, &handle_listener, win);
}

// 当合成器销毁管理器时触发
static void foreign_toplevel_manager_handle_finished(void *data,
             struct treeland_foreign_toplevel_manager_v1 *manager) {
    
    printf("Treeland 外界窗口管理器已结束。\n");
}

// 实例化 Listener 结构体
static const struct treeland_foreign_toplevel_manager_v1_listener foreign_toplevel_manager_listener = {
    .toplevel = foreign_toplevel_manager_handle_toplevel,
    .finished = foreign_toplevel_manager_handle_finished,
};

// --- wl_registry 事件监听器 ---
static void registry_handle_global(void *data, struct wl_registry *registry, uint32_t name,
                                   const char *interface, uint32_t version) {
    struct state *state = data;
    printf("Found global: %s (version %u)\n", interface, version);
 
    if (strcmp(interface, wl_compositor_interface.name) == 0) {
        state->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    } else if (strcmp(interface, wl_shm_interface.name) == 0) {
        state->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    } else if (strcmp(interface, xdg_wm_base_interface.name) == 0) {
        state->xdg_wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface, 1);
        xdg_wm_base_add_listener(state->xdg_wm_base, &xdg_wm_base_listener, state);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0) {
        state->decoration_manager = wl_registry_bind(registry, name, &zxdg_decoration_manager_v1_interface, 1);
    } else if (strcmp(interface, treeland_foreign_toplevel_manager_v1_interface.name) == 0) {
        state->foreign_toplevel_manager = wl_registry_bind(registry, name, &treeland_foreign_toplevel_manager_v1_interface, 1);

        treeland_foreign_toplevel_manager_v1_add_listener(
            state->foreign_toplevel_manager, 
            &foreign_toplevel_manager_listener, 
            state
        );
    }
}

static void registry_handle_global_remove(void *data, struct wl_registry *registry, uint32_t name) {
    // This space is for rent
}

static const struct wl_registry_listener registry_listener = {
    .global = registry_handle_global,
    .global_remove = registry_handle_global_remove,
};

int main(int argc, char **argv) {
    struct state state = {0};
    state.width = 640;
    state.height = 480;
    state.running = 1;

    wl_list_init(&state.toplevel_list);
 
    // 1. 连接到Wayland display
    state.display = wl_display_connect(NULL);
    if (state.display == NULL) {
        fprintf(stderr, "Can't connect to a Wayland display\n");
        return 1;
    }
 
    // 2. 获取registry，用于发现全局对象
    state.registry = wl_display_get_registry(state.display);
    wl_registry_add_listener(state.registry, &registry_listener, &state);
 
    // 3. 同步，等待服务器处理我们的请求并发送全局对象事件
    wl_display_dispatch(state.display);
    wl_display_roundtrip(state.display);
 
    // 检查是否成功绑定了必要的全局对象
    if (state.compositor == NULL || state.shm == NULL || state.xdg_wm_base == NULL) {
        fprintf(stderr, "Can't find compositor, shm or xdg_wm_base\n");
        return 1;
    }
 
    // 4. 创建Wayland表面
    state.surface = wl_compositor_create_surface(state.compositor);
 
    // 5. 通过xdg-shell将表面设置为toplevel窗口
    state.xdg_surface = xdg_wm_base_get_xdg_surface(state.xdg_wm_base, state.surface);
    xdg_surface_add_listener(state.xdg_surface, &xdg_surface_listener, &state);
    state.xdg_toplevel = xdg_surface_get_toplevel(state.xdg_surface);
    xdg_toplevel_add_listener(state.xdg_toplevel, &xdg_toplevel_listener, &state);
 
    // 设置窗口标题
    xdg_toplevel_set_title(state.xdg_toplevel, "Wayland Hello World");
 
    // 进行装饰协商
    if (state.decoration_manager) {
        printf("Decoration manager found. Negotiating...\n");
        // 为我们的窗口获取一个装饰对象
        state.toplevel_decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(
            state.decoration_manager, state.xdg_toplevel);
        
        // 请求使用 SERVER_SIDE_DECORATION
        zxdg_toplevel_decoration_v1_set_mode(
            state.toplevel_decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    } else {
        printf("Decoration manager not found. Cannot negotiate decorations.\n");
    }
 
    // 提交表面，让xdg-shell知道我们已经配置好了
    wl_surface_commit(state.surface);
 
    // 主事件循环
    while (state.running && wl_display_dispatch(state.display) != -1) {
        // 事件处理都在监听器回调中完成
    }
 
    // 清理资源
    printf("Cleaning up...\n");
    if (state.toplevel_decoration) zxdg_toplevel_decoration_v1_destroy(state.toplevel_decoration);
    if (state.decoration_manager) zxdg_decoration_manager_v1_destroy(state.decoration_manager);
    if (state.foreign_toplevel_manager) treeland_foreign_toplevel_manager_v1_destroy(state.foreign_toplevel_manager);
    if (state.buffer) wl_buffer_destroy(state.buffer);
    if (state.xdg_toplevel) xdg_toplevel_destroy(state.xdg_toplevel);
    if (state.xdg_surface) xdg_surface_destroy(state.xdg_surface);
    if (state.surface) wl_surface_destroy(state.surface);
    if (state.xdg_wm_base) xdg_wm_base_destroy(state.xdg_wm_base);
    if (state.shm) wl_shm_destroy(state.shm);
    if (state.compositor) wl_compositor_destroy(state.compositor);
    if (state.registry) wl_registry_destroy(state.registry);
    if (state.display) wl_display_disconnect(state.display);
 
    return 0;
}
