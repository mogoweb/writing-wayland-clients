## 输入（Input）

一旦你完成了 `wl_pointer` 的设置，处理输入事件就会变得非常简单。我们先从把所有输入事件打印出来开始：

```c
void pointer_enter_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface,
    wl_fixed_t x,
    wl_fixed_t y
)
{
    wl_pointer_set_cursor(pointer, serial, cursor_surface,
        cursor_image->hotspot_x, cursor_image->hotspot_y);

    printf("enter:\t%d %d\n",
        wl_fixed_to_int(x),
        wl_fixed_to_int(y));
}

void pointer_leave_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    struct wl_surface *surface
)
{
    printf("leave\n");
}

void pointer_motion_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    wl_fixed_t x,
    wl_fixed_t y
)
{
    printf("motion:\t%d %d\n",
        wl_fixed_to_int(x),
        wl_fixed_to_int(y));
}

void pointer_button_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t serial,
    uint32_t time,
    uint32_t button,
    uint32_t state
)
{
    printf("button: 0x%x state: %d\n", button, state);
}

void pointer_axis_handler
(
    void *data,
    struct wl_pointer *pointer,
    uint32_t time,
    uint32_t axis,
    wl_fixed_t value
)
{
    printf("axis: %d %f\n", axis, wl_fixed_to_double(value));
}
```

输出结果大致如下：

```bash
enter:  76 5
motion: 76 5
motion: 77 13
motion: 78 23
motion: 79 27
motion: 80 28
button: 0x110 state: 1
button: 0x110 state: 0
button: 0x112 state: 1
button: 0x112 state: 0
button: 0x111 state: 1
button: 0x111 state: 0
axis: 0 10.000000
axis: 1 0.976562
axis: 0 -0.390625
axis: 1 1.328125
motion: 81 28
motion: 83 29
motion: 84 29
motion: 86 30
motion: 86 29
motion: 87 23
motion: 88 17
motion: 89 10
motion: 91 1
leave
```

可以看到，在 `enter` 和 `motion` 事件中，我们拿到的是相对于 surface 的本地坐标。这些坐标的类型是 `wl_fixed_t`，属于内部类型，可以通过 `wl_fixed_to_int`、`wl_fixed_to_double`、`wl_fixed_from_int` 和 `wl_fixed_from_double` 等函数在 `int` 和 `double` 之间进行转换。

`state` 参数的取值如下：

```c
WL_POINTER_BUTTON_STATE_PRESSED  = 1
WL_POINTER_BUTTON_STATE_RELEASED = 0
```

`button` 参数使用的是 `linux/input-event-codes.h` 中定义的按键值，常见的包括：

```c
BTN_LEFT   = 0x110
BTN_RIGHT  = 0x111
BTN_MIDDLE = 0x112
```

当然还可能存在其他按键，一些高级鼠标最多可以支持二十个按钮。

`axis` 参数的取值为：

```c
WL_POINTER_AXIS_VERTICAL_SCROLL   = 0
WL_POINTER_AXIS_HORIZONTAL_SCROLL = 1
```

TODO：键盘
