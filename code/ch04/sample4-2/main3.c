#include <stdio.h>
#include <string.h>
#include <stdlib.h>

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

// 函数前向声明
static void create_and_attach_buffer(int width, int height);

// xdg_surface configure
static void xdg_surface_configure(void *data, struct xdg_surface *xdg_surface, uint32_t serial) {
    printf("Received xdg_surface.configure event\n"); // 调试输出
    // 必须 ack configure
    xdg_surface_ack_configure(xdg_surface, serial);

    // 在这里创建并附加 buffer，然后 commit
    // 理想情况下，应该根据 configure 事件传递的尺寸来创建 buffer
    create_and_attach_buffer(200, 200);
    wl_surface_attach(surface, buffer, 0, 0);
    wl_surface_commit(surface);
}

// xdg_toplevel configure (可选，但推荐实现)
static void xdg_toplevel_configure(void *data, struct xdg_toplevel *xdg_toplevel, int32_t width, int32_t height, struct wl_array *states) {
    // 合成器可能会在这里建议窗口大小
    // 如果 width 和 height 不为0，可以根据这个大小重新创建 buffer
}

// xdg_toplevel close
static void xdg_toplevel_close(void *data, struct xdg_toplevel *xdg_toplevel) {
    // 用户点击了关闭按钮
    // 在这里退出主循环
    exit(0);
}

static const struct xdg_surface_listener xdg_surface_listener = {
    .configure = xdg_surface_configure
};

// 添加 toplevel listener 来处理关闭事件
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    .configure = xdg_toplevel_configure,
    .close = xdg_toplevel_close,
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

// 将 Buffer 创建逻辑封装成一个函数
static void create_and_attach_buffer(int width, int height) {
    if (buffer) {
        // 如果 buffer 已存在，先销毁（在窗口大小调整时需要）
        wl_buffer_destroy(buffer);
    }

    int stride = width * 4;
    int size = stride * height;

    // 创建共享内存 buffer
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    if (fd == -1) {
        perror("memfd_create failed");
        exit(1);
    }
    ftruncate(fd, size);

    // 将共享内存映射到进程地址空间
    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap failed");
        close(fd);
        exit(1);
    }

    for (int x = 0; x < width; x++) {
        for (int y = 0; y < height; y++) {

            struct pixel {
                // little-endian ARGB
                unsigned char blue;
                unsigned char green;
                unsigned char red;
                unsigned char alpha;
            } *px = (struct pixel *) (data + y * stride + x * 4);

            // draw a cross
            if ((80 < x && x < 120) || (80 < y && y < 120)) {
                // gradient from blue at the top to white at the bottom
                px->alpha = 255;
                px->red = (double) y / height * 255;
                px->green = px->red;
                px->blue = 255;
            } else {
                // transparent
                px->alpha = 0;
            }
        }
    }
    munmap(data, size); // 解除映射，因为 wl_shm_pool 已经接管

    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);
    buffer = wl_shm_pool_create_buffer(pool,
        0, width, height, stride, WL_SHM_FORMAT_ARGB8888);

    // pool在创建buffer后就可以销毁了
    wl_shm_pool_destroy(pool);
    // fd在创建pool后就可以关闭了
    close(fd);
}

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    if (!display) {
        fprintf(stderr, "无法连接 Wayland 显示服务器\n");
        return -1;
    }

    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    wl_display_roundtrip(display);

    if (!compositor || !shm || !wm_base) {
        fprintf(stderr, "缺少必要的全局对象 (compositor/shm/wm_base)\n");
        return -1;
    }

    xdg_wm_base_add_listener(wm_base, &wm_base_listener, NULL);

    surface = wl_compositor_create_surface(compositor);

    struct xdg_surface *xdg_surface = xdg_wm_base_get_xdg_surface(wm_base, surface);
    struct xdg_toplevel *toplevel = xdg_surface_get_toplevel(xdg_surface);

    xdg_toplevel_set_title(toplevel, "黑色窗口");

    xdg_surface_add_listener(xdg_surface, &xdg_surface_listener, NULL);
    // *** 关键修改 2: 添加 toplevel listener ***
    // 这样才能响应关闭按钮等事件
    xdg_toplevel_add_listener(toplevel, &xdg_toplevel_listener, NULL);


    // *** 关键修改 1: 发送初始化的 commit ***
    // 这次 commit 告诉合成器，我们已经完成了初始设置，请发送 configure 事件
    wl_surface_commit(surface);
    printf("Initial wl_surface_commit sent\n"); // 调试输出


    // 注释掉在这里创建 buffer 的代码，将其移到 configure 回调中
    /*
    int width = 200;
    int height = 200;
    create_and_attach_buffer(width, height);
    */

    while (wl_display_dispatch(display) != -1) {
        // 主循环等待事件
    }

    // 清理资源 (虽然在这个例子中，程序退出时会自动清理)
    if (buffer) wl_buffer_destroy(buffer);
    if (toplevel) xdg_toplevel_destroy(toplevel);
    if (xdg_surface) xdg_surface_destroy(xdg_surface);
    if (surface) wl_surface_destroy(surface);
    if (display) wl_display_disconnect(display);


    return 0;
}
