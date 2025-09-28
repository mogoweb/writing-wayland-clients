#include <syscall.h>
#include <unistd.h>
#include <sys/mman.h>

#include <stdio.h>
#include <string.h>
#include <wayland-client.h>

struct wl_compositor *compositor;
struct wl_shm *shm;
struct wl_shell *shell;

void registry_global_handler
(
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
    }
}

void registry_global_remove_handler
(
    void *data,
    struct wl_registry *registry,
    uint32_t name
) {}

const struct wl_registry_listener registry_listener = {
    .global = registry_global_handler,
    .global_remove = registry_global_remove_handler
};

int main(void)
{
    struct wl_display *display = wl_display_connect(NULL);
    struct wl_registry *registry = wl_display_get_registry(display);
    wl_registry_add_listener(registry, &registry_listener, NULL);

    // 等待“初始”的全局对象出现
    wl_display_roundtrip(display);

    // 我们需要的对象应该已经就绪！
    if (compositor && shm) {
        printf("Got them all!\n");
    } else {
        printf("Some required globals unavailable\n");
        return 1;
    }

    int width = 200;
    int height = 200;
    int stride = width * 4;
    int size = stride * height;  // 字节数

    // 打开匿名文件并写入零字节
    int fd = syscall(SYS_memfd_create, "buffer", 0);
    ftruncate(fd, size);

    // 映射到内存
    unsigned char *data = mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);

    // 创建共享内存池
    struct wl_shm_pool *pool = wl_shm_create_pool(shm, fd, size);

    // 在池中分配缓冲区
    struct wl_buffer *buffer = wl_shm_pool_create_buffer(pool,
        0, width, height, stride, WL_SHM_FORMAT_XRGB8888);

    while (1) {
        wl_display_dispatch(display);
    }
}

