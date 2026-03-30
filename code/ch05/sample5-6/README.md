# Sample 5-6: xdg_activation_v1 Window Activation Demo

This sample demonstrates the use of the `xdg_activation_v1` protocol to programmatically request focus/activation of one window from another window.

## Features

- Creates two toplevel windows: a "Main Window" (green) and a "Tool Window" (gray)
- The Tool Window contains a red "button" in the center (50x50 pixels)
- Clicking the red button requests activation of the Main Window
- Uses the xdg_activation_v1 protocol with proper token-based authorization
- Demonstrates user-initiated window activation (security feature)

## Building

```bash
make
```

## Running

```bash
./runme
```

Click the red button in the Tool Window to activate and bring the Main Window to front.

## Protocol Details

The `xdg_activation_v1` protocol is designed for user-initiated window activation requests, providing a security model where activation tokens are tied to user input events.

Key workflow:
1. Create an activation token using `xdg_activation_v1_get_activation_token()`
2. Set the trigger information (input serial and seat) via `xdg_activation_token_v1_set_serial()`
3. Set the source surface via `xdg_activation_token_v1_set_surface()`
4. Request the token via `xdg_activation_token_v1_commit()`
5. Receive token in the `done` callback
6. Activate the target surface using `xdg_activation_v1_activate()`

## Security Model

The activation token ensures that window activation is tied to a user action:
- The serial from the input event (mouse click) proves user intent
- The compositor can verify the token was issued based on real user input
- Prevents applications from stealing focus unexpectedly

## Notes

- Requires compositor support for `xdg_activation_v1` protocol
- The activation token is one-time use
- Activation may be denied by the compositor based on policies
- This is the replacement for the older `xdg_foreign_v2` focus transfer pattern
