### Input

Once you've set up wl_pointer, handling input events is very easy. Let's start by logging all the input:

```
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
The output looks like this:

```
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

As you can see, we get surface-local coordinates with enter and motion events. Those are of opaque type wl_fixed_t which we can convert to and from int and double using wl_fixed_to_int, wl_fixed_to_double, wl_fixed_from_int and wl_fixed_from_double functions.

The state argument can be:

```
WL_POINTER_BUTTON_STATE_PRESSED = 1
WL_POINTER_BUTTON_STATE_RELEASED = 0
```

The button argument uses the same values as defined in linux/input-event-codes.h. In particular,

```
BTN_LEFT = 0x110
BTN_RIGHT = 0x111
BTN_MIDDLE = 0x112
```

There may be others, however; advanced mice may have up to twenty buttons.

The axis argument can be

```
WL_POINTER_AXIS_VERTICAL_SCROLL = 0
WL_POINTER_AXIS_HORIZONTAL_SCROLL = 1
```

TODO: keyboard