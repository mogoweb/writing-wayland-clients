# Sample 5-5: xdg_dialog_v1 Dialog Demo

This sample demonstrates the use of the `xdg_dialog_v1` protocol to create a modal dialog window that is a child of a parent toplevel window.

## Features

- Creates a parent window with a blue background
- Creates a child dialog window with a red background
- Uses `xdg_toplevel_set_parent()` to establish the parent-child relationship
- Uses `xdg_dialog_v1_set_modal()` to mark the dialog as modal
- The compositor may display the dialog centered over the parent window

## Building

```bash
make
```

## Running

```bash
./runme
```

Press Enter after the parent window appears to create the dialog. Press Enter again to exit.

## Protocol Details

The `xdg_dialog_v1` protocol provides two main interfaces:

1. **xdg_wm_dialog_v1**: Global object used to create dialog objects
2. **xdg_dialog_v1**: Dialog object that provides hints for the dialog window

Key methods:
- `xdg_wm_dialog_v1_get_xdg_dialog()`: Create a dialog object for a toplevel
- `xdg_dialog_v1_set_modal()`: Mark the dialog as modal
- `xdg_dialog_v1_unset_modal()`: Remove modal hint

## Notes

- The dialog must have a parent toplevel set via `xdg_toplevel_set_parent()`
- Client must implement its own event filtering logic for the parent toplevel
- Compositor may choose its own policy for event delivery to the parent window
- The protocol requires compositor support - not all compositors implement it yet
