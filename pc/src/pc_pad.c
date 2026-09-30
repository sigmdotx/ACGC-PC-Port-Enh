/* pc_pad.c - GC controller input via SDL gamepad + keyboard */
#include "pc_platform.h"
#include "pc_typing.h"
#include "pc_keybindings.h"
#include "pc_settings.h"
#include <dolphin/pad.h>

/* analog stick constants */
#define STICK_MAGNITUDE 80
#define RUMBLE_DURATION_MS 200

static SDL_GameController* g_controllers[2] = { NULL, NULL };
/* deadzone percent (0-40) -> raw SDL axis threshold */
static int deadzone_threshold(int percent) {
    if (percent < 0)
        percent = 0;
    if (percent > 90)
        percent = 90;
    return percent * 32767 / 100;
}

/* is a remappable pad binding currently held? */
static int pad_code_pressed(SDL_GameController* controller, PCPadCode code) {
    if (code < 0)
        return 0;
    if (controller == NULL)
        return 0;

    if (code & PC_PAD_AXIS_BIT) {
        return SDL_GameControllerGetAxis(controller, (SDL_GameControllerAxis)(code & 0xFF)) > PC_PAD_AXIS_PRESS;
    }
    return SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)code);
}

/* analog trigger value for the L/R binding (digital bindings read as full press) */
static u8 pad_trigger_value(SDL_GameController* controller, PCPadCode code) {
    if (code < 0)
        return 0;
    if (controller == NULL)
        return 0;
    if (code & PC_PAD_AXIS_BIT) {
        s16 v = SDL_GameControllerGetAxis(controller, (SDL_GameControllerAxis)(code & 0xFF));
        if (v < 0)
            v = 0;
        return (u8)(v >> 7);
    }
    return SDL_GameControllerGetButton(controller, (SDL_GameControllerButton)code) ? 255 : 0;
}

BOOL PADInit(void) {
    int controller_slot = 0;
    int joystick_count = SDL_NumJoysticks();

    printf(
        "[MVP0] SDL joysticks detected: %d\n",
        joystick_count
    );

    for (int i = 0;
         i < joystick_count && controller_slot < 2;
         i++) {

        int is_game_controller = SDL_IsGameController(i);

        printf(
            "[MVP0] Device %d: is_game_controller=%d",
            i,
            is_game_controller
        );

        if (is_game_controller) {
            const char* name =
                SDL_GameControllerNameForIndex(i);

            printf(
                " name=%s\n",
                name != NULL ? name : "(unknown)"
            );

            SDL_GameController* controller =
                SDL_GameControllerOpen(i);

            if (controller != NULL) {
                g_controllers[controller_slot] =
                    controller;

                printf(
                    "[MVP0] Controller slot %d opened: %s\n",
                    controller_slot,
                    SDL_GameControllerName(controller)
                );

                controller_slot++;
            } else {
                printf(
                    "[MVP0] Failed to open device %d: %s\n",
                    i,
                    SDL_GetError()
                );
            }
        } else {
            printf("\n");
        }
    }

    printf(
        "[MVP0] Controllers opened: %d\n",
        controller_slot
    );

    return TRUE;
}

u32 PADRead(PADStatus* status) {
    memset(status, 0, sizeof(PADStatus) * 4);

    /*
     * Initially mark every GameCube controller port as disconnected.
     * Port 0 will always be considered available because keyboard input
     * can act as Player 1 even when no physical controller is connected.
     */
    for (int i = 0; i < 4; i++) {
        status[i].err = PAD_ERR_NO_CONTROLLER;
    }

    status[0].err = PAD_ERR_NONE;

    /*
     * PADRead returns a bit mask indicating which controller ports
     * are available. Port 0 is always available because of keyboard input.
     */
    u32 connected_mask = PAD_CHAN0_BIT;

    /*
     * Separate state for each local player.
     *
     * Index 0 = Player 1
     * Index 1 = Player 2
     */
    u16 buttons[2] = { 0, 0 };

    s8 stickX[2] = { 0, 0 };
    s8 stickY[2] = { 0, 0 };

    s8 cstickX[2] = { 0, 0 };
    s8 cstickY[2] = { 0, 0 };

    /*
     * ---------------------------------------------------------
     * Keyboard + mouse
     * ---------------------------------------------------------
     *
     * Keyboard input belongs ONLY to Player 1.
     */
    const u8* keys = SDL_GetKeyboardState(NULL);
    u32 mouse = SDL_GetMouseState(NULL, NULL);

    if (!(g_pc_typing_mode && g_pc_editor_active)) {
#define INPUT_PRESSED(code) \
    (((code) & PC_INPUT_MOUSE_BIT) ? (mouse & SDL_BUTTON((code) & 0xFF)) : keys[(SDL_Scancode)(code)])

        PCKeybindings* kb = &g_pc_keybindings;

        /* Player 1 buttons */
        if (INPUT_PRESSED(kb->a))
            buttons[0] |= PAD_BUTTON_A;
        if (INPUT_PRESSED(kb->b))
            buttons[0] |= PAD_BUTTON_B;
        if (INPUT_PRESSED(kb->x))
            buttons[0] |= PAD_BUTTON_X;
        if (INPUT_PRESSED(kb->y))
            buttons[0] |= PAD_BUTTON_Y;
        if (INPUT_PRESSED(kb->start))
            buttons[0] |= PAD_BUTTON_START;
        if (INPUT_PRESSED(kb->z))
            buttons[0] |= PAD_TRIGGER_Z;
        if (INPUT_PRESSED(kb->l))
            buttons[0] |= PAD_TRIGGER_L;
        if (INPUT_PRESSED(kb->r))
            buttons[0] |= PAD_TRIGGER_R;

        /* Player 1 main stick */
        if (INPUT_PRESSED(kb->stick_up)) {
            stickY[0] += STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->stick_down)) {
            stickY[0] -= STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->stick_left)) {
            stickX[0] -= STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->stick_right)) {
            stickX[0] += STICK_MAGNITUDE;
        }

        /* Player 1 C-stick */
        if (INPUT_PRESSED(kb->cstick_up)) {
            cstickY[0] += STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->cstick_down)) {
            cstickY[0] -= STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->cstick_left)) {
            cstickX[0] -= STICK_MAGNITUDE;
        }

        if (INPUT_PRESSED(kb->cstick_right)) {
            cstickX[0] += STICK_MAGNITUDE;
        }

        /* Player 1 D-pad */
        if (INPUT_PRESSED(kb->dpad_up)) {
            buttons[0] |= PAD_BUTTON_UP;
        }

        if (INPUT_PRESSED(kb->dpad_down)) {
            buttons[0] |= PAD_BUTTON_DOWN;
        }

        if (INPUT_PRESSED(kb->dpad_left)) {
            buttons[0] |= PAD_BUTTON_LEFT;
        }

        if (INPUT_PRESSED(kb->dpad_right)) {
            buttons[0] |= PAD_BUTTON_RIGHT;
        }

#undef INPUT_PRESSED
    }

    /*
     * ---------------------------------------------------------
     * Physical controllers
     * ---------------------------------------------------------
     *
     * g_controllers[0] -> Player 1
     * g_controllers[1] -> Player 2
     */
    PCPadBindings* pb = &g_pc_padbindings;

    int stick_dz = deadzone_threshold(g_pc_settings.stick_deadzone);

    int cstick_dz = deadzone_threshold(g_pc_settings.cstick_deadzone);

    for (int player = 0; player < 2; player++) {
        SDL_GameController* controller = g_controllers[player];

        /*
         * Detect a controller that was disconnected while the game
         * was running.
         *
         * For MVP 0.1 we close it and clear the slot.
         * Reconnecting it will require restarting the game for now.
         */
        if (controller != NULL && !SDL_GameControllerGetAttached(controller)) {

            SDL_GameControllerClose(controller);
            g_controllers[player] = NULL;
            controller = NULL;
        }

        if (controller == NULL) {
            continue;
        }

        /*
         * Mark this GameCube controller port as connected.
         *
         * PAD_CHAN0_BIT = port 0
         * PAD_CHAN0_BIT >> 1 = port 1
         */
        connected_mask |= (PAD_CHAN0_BIT >> player);

        status[player].err = PAD_ERR_NONE;

        /* Buttons */
        if (pad_code_pressed(controller, pb->a)) {
            buttons[player] |= PAD_BUTTON_A;
        }

        if (pad_code_pressed(controller, pb->b)) {
            buttons[player] |= PAD_BUTTON_B;
        }

        if (pad_code_pressed(controller, pb->x)) {
            buttons[player] |= PAD_BUTTON_X;
        }

        if (pad_code_pressed(controller, pb->y)) {
            buttons[player] |= PAD_BUTTON_Y;
        }

        if (pad_code_pressed(controller, pb->start)) {
            buttons[player] |= PAD_BUTTON_START;
        }

        if (pad_code_pressed(controller, pb->z)) {
            buttons[player] |= PAD_TRIGGER_Z;
        }

        if (pad_code_pressed(controller, pb->l)) {
            buttons[player] |= PAD_TRIGGER_L;
        }

        if (pad_code_pressed(controller, pb->r)) {
            buttons[player] |= PAD_TRIGGER_R;
        }

        if (pad_code_pressed(controller, pb->dpad_up)) {
            buttons[player] |= PAD_BUTTON_UP;
        }

        if (pad_code_pressed(controller, pb->dpad_down)) {
            buttons[player] |= PAD_BUTTON_DOWN;
        }

        if (pad_code_pressed(controller, pb->dpad_left)) {
            buttons[player] |= PAD_BUTTON_LEFT;
        }

        if (pad_code_pressed(controller, pb->dpad_right)) {
            buttons[player] |= PAD_BUTTON_RIGHT;
        }

        /*
         * Main analog stick
         */
        s16 lx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTX);

        s16 ly = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_LEFTY);

        if (abs(lx) > stick_dz) {
            int sx = lx >> 8;

            if (sx > 127) {
                sx = 127;
            } else if (sx < -128) {
                sx = -128;
            }

            stickX[player] = (s8)sx;
        }

        if (abs(ly) > stick_dz) {
            int sy = -(ly >> 8);

            if (sy > 127) {
                sy = 127;
            } else if (sy < -128) {
                sy = -128;
            }

            stickY[player] = (s8)sy;
        }

        /*
         * C-stick / right analog stick
         */
        s16 rx = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTX);

        s16 ry = SDL_GameControllerGetAxis(controller, SDL_CONTROLLER_AXIS_RIGHTY);

        if (abs(rx) > cstick_dz) {
            int srx = rx >> 8;

            if (srx > 127) {
                srx = 127;
            } else if (srx < -128) {
                srx = -128;
            }

            cstickX[player] = (s8)srx;
        }

        if (abs(ry) > cstick_dz) {
            int sry = -(ry >> 8);

            if (sry > 127) {
                sry = 127;
            } else if (sry < -128) {
                sry = -128;
            }

            cstickY[player] = (s8)sry;
        }

        /*
         * Analog L/R trigger values
         */
        status[player].triggerLeft = pad_trigger_value(controller, pb->l);

        status[player].triggerRight = pad_trigger_value(controller, pb->r);
    }

    /*
     * ---------------------------------------------------------
     * Copy our local-player state into GameCube PADStatus slots.
     * ---------------------------------------------------------
     */
    for (int player = 0; player < 2; player++) {
        status[player].button = buttons[player];

        status[player].stickX = stickX[player];
        status[player].stickY = stickY[player];

        status[player].substickX = cstickX[player];
        status[player].substickY = cstickY[player];
    }

    return connected_mask;
}

void PADControlMotor(s32 chan, u32 command) {
    if (chan < 0 || chan >= 2) {
        return;
    }

    SDL_GameController* controller = g_controllers[chan];

    if (controller == NULL) {
        return;
    }

    u16 intensity = (command == PAD_MOTOR_RUMBLE) ? 0xFFFF : 0;

    SDL_GameControllerRumble(controller, intensity, intensity, RUMBLE_DURATION_MS);
}

void PADControlAllMotors(const u32* commands) {
    PADControlMotor(0, commands[0]);
    PADControlMotor(1, commands[1]);
}

void PADCleanup(void) {
    for (int i = 0; i < 2; i++) {
        if (g_controllers[i] != NULL) {
            SDL_GameControllerClose(g_controllers[i]);
            g_controllers[i] = NULL;
        }
    }
}

BOOL PADReset(u32 mask) {
    (void)mask;
    return TRUE;
}
BOOL PADRecalibrate(u32 mask) {
    (void)mask;
    return TRUE;
}
BOOL PADSync(void) {
    return TRUE;
}
void PADSetSpec(u32 spec) {
    (void)spec;
}
void PADSetAnalogMode(u32 mode) {
    (void)mode;
}
/* PADClamp compiled from decomp: src/static/dolphin/pad/Padclamp.c */
BOOL PADGetType(s32 chan, u32* type) {
    if (type)
        *type = 0x09000000;
    return TRUE;
}
