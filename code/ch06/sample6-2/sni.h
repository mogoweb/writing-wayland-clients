#pragma once

#include <cairo/cairo.h>

// 1. 定义回调函数指针：当用户在托盘点击“激活”时触发
typedef void (*sni_activate_callback)(void *user_data);

typedef struct sni_manager sni_manager_t;

// 2. 初始化与销毁
sni_manager_t* sni_manager_create(const char *appid, const char *icon_name);
void sni_manager_destroy(sni_manager_t *sni);

// 3. 设置回调
void sni_manager_set_on_activate(sni_manager_t *sni, sni_activate_callback cb, void *user_data);

// 4. 图片处理
void sni_manager_set_icon_pixmap(sni_manager_t *sni, cairo_surface_t *surface);

// 5. 事件循环集成
int sni_manager_get_fd(sni_manager_t *sni);
void sni_manager_dispatch(sni_manager_t *sni);

// 6. 状态同步
void sni_manager_set_status(sni_manager_t *sni, const char *status); // "Active", "Passive"
