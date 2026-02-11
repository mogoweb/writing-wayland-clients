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
    _Bool compositor_supports_ssd;
 
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
 
    // 清空缓冲区内存
    memset(state->shm_data, 0, size);
 
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
    }
    // 注意: 我们不在这里绘图，因为我们会在xdg_surface的configure事件后绘图
}

static void xdg_toplevel_handle_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    struct state *state = data;
    // 合成器通知我们用户点击了关闭按钮
    state->running = 0;
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

// -- xdg_toplevel_decoration 的事件监听器
static void decoration_handle_configure(void *data,
                                        struct zxdg_toplevel_decoration_v1 *decoration,
                                        uint32_t mode)
{
    struct state *state = data;
    // 这是协商的核心！合成器通过这个事件告诉我们它最终决定的装饰模式。
    printf("==> Compositor negotiated decoration mode: ");
    switch (mode) {
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE:
            printf("Client-Side (we must draw our own!)\n");
            state->compositor_supports_ssd = 0;
            break;
        case ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE:
            printf("Server-Side (compositor will draw for us!)\n");
            state->compositor_supports_ssd = 1;
            break;
        default:
            printf("Unknown\n");
            state->compositor_supports_ssd = 0;
            break;
    }

    // 如果合成器支持 SSD，再请求服务器端装饰
    if (state->compositor_supports_ssd) {
        zxdg_toplevel_decoration_v1_set_mode(decoration, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = {
    .configure = decoration_handle_configure,
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
        // 添加监听器以接收合成器的决定
        zxdg_toplevel_decoration_v1_add_listener(
            state.toplevel_decoration, &decoration_listener, &state);
        
        // 初始请求可以使用 CLIENT_SIDE 或者 UNSPECIFIED
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
