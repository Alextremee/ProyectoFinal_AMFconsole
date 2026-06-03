/*
 * gamepad-bridge v5 FINAL: Mapeo confirmado con cfg real de Mednafen
 * NES defaults (después de Alt+Shift+1):
 *   A button -> keycode 4 = KEY_A
 *   B button -> keycode 9 = KEY_F
 *   Start -> keycode 40 = KEY_ENTER
 *   Select -> keycode 229 = KEY_RIGHTSHIFT
 *   Up -> keycode 82 = KEY_UP
 *   Down -> keycode 81 = KEY_DOWN
 *   Left -> keycode 80 = KEY_LEFT
 *   Right -> keycode 79 = KEY_RIGHT
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/joystick.h>
#include <sys/ioctl.h>
#include <signal.h>

#define JOY_DEV "/dev/input/js0"
#define UINPUT_DEV "/dev/uinput"
#define READY_FILE "/tmp/gamepad-bridge-ready"
#define THRESHOLD 16000

typedef enum { AX_CENTER = 0, AX_NEG = -1, AX_POS = 1 } AxisState;

static int running = 1;
static AxisState axis_x_state = AX_CENTER;
static AxisState axis_y_state = AX_CENTER;

static void handle_signal(int s) { (void)s; running = 0; }

/* Mapeo confirmado contra el cfg que Mednafen guardó tras Alt+Shift+1 */
static int btn_to_key(int b) {
    switch (b) {
        case 0:  return KEY_A;          /* CRUZ      -> A button NES */
        case 1:  return KEY_X;          /* CIRCULO   -> X SNES (extra) */
        case 2:  return KEY_Z;          /* TRIANGULO -> Y SNES (extra) */
        case 3:  return KEY_G;          /* CUADRADO  -> B button NES */
        case 4:  return KEY_Q;          /* L1        -> L SNES (extra) */
        case 5:  return KEY_E;          /* R1        -> R SNES (extra) */
        case 8:  return KEY_RIGHTSHIFT; /* SHARE     -> Select */
        case 9:  return KEY_ENTER;      /* OPTIONS   -> Start */
        case 10: return KEY_F12;        /* PS        -> Salir Mednafen */
        default: return 0;
    }
}

static void emit(int fd, int type, int code, int val) {
    struct input_event ie;
    memset(&ie, 0, sizeof(ie));
    ie.type = type;
    ie.code = code;
    ie.value = val;
    write(fd, &ie, sizeof(ie));
}

static void emit_key(int fd, int key, int press) {
    emit(fd, EV_KEY, key, press);
    emit(fd, EV_SYN, SYN_REPORT, 0);
}

static void handle_axis(int fd, AxisState* state, int v,
                        int key_neg, int key_pos) {
    AxisState new_state;
    if (v < -THRESHOLD) new_state = AX_NEG;
    else if (v > THRESHOLD) new_state = AX_POS;
    else new_state = AX_CENTER;
    if (new_state == *state) return;
    if (*state == AX_NEG) emit_key(fd, key_neg, 0);
    else if (*state == AX_POS) emit_key(fd, key_pos, 0);
    if (new_state == AX_NEG) emit_key(fd, key_neg, 1);
    else if (new_state == AX_POS) emit_key(fd, key_pos, 1);
    *state = new_state;
}

int main(void) {
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);

    int jfd = -1;
    for (int i = 0; i < 60 && running; i++) {
        jfd = open(JOY_DEV, O_RDONLY | O_NONBLOCK);
        if (jfd >= 0) break;
        sleep(1);
    }
    if (jfd < 0) return 1;

    int ufd = open(UINPUT_DEV, O_WRONLY | O_NONBLOCK);
    if (ufd < 0) { close(jfd); return 1; }

    ioctl(ufd, UI_SET_EVBIT, EV_KEY);
    ioctl(ufd, UI_SET_EVBIT, EV_SYN);
    ioctl(ufd, UI_SET_EVBIT, EV_MSC);
    ioctl(ufd, UI_SET_MSCBIT, MSC_SCAN);
    ioctl(ufd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

    int keys[] = {
        KEY_A, KEY_F, KEY_X, KEY_Z, KEY_Q, KEY_E,
        KEY_ENTER, KEY_TAB, KEY_SPACE, KEY_BACKSPACE,
        KEY_LEFTSHIFT, KEY_RIGHTSHIFT, KEY_LEFTCTRL, KEY_RIGHTCTRL,
        KEY_LEFTALT, KEY_RIGHTALT,
        KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6,
        KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
        KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT,
        KEY_ESC, KEY_KP0, KEY_KP1, KEY_KP2, KEY_KP3
    };
    for (size_t i = 0; i < sizeof(keys)/sizeof(keys[0]); i++)
        ioctl(ufd, UI_SET_KEYBIT, keys[i]);

    struct uinput_setup usetup;
    memset(&usetup, 0, sizeof(usetup));
    usetup.id.bustype = BUS_USB;
    usetup.id.vendor  = 0x413c;
    usetup.id.product = 0x2003;
    usetup.id.version = 0x0110;
    strcpy(usetup.name, "Dell USB Keyboard Bridge");
    ioctl(ufd, UI_DEV_SETUP, &usetup);
    ioctl(ufd, UI_DEV_CREATE);

    sleep(2);

    int rfd = open(READY_FILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (rfd >= 0) { write(rfd, "ready\n", 6); close(rfd); }

    struct js_event je;
    while (running) {
        ssize_t n = read(jfd, &je, sizeof(je));
        if (n != sizeof(je)) { usleep(5000); continue; }
        je.type &= ~JS_EVENT_INIT;

        if (je.type == JS_EVENT_BUTTON) {
            int key = btn_to_key(je.number);
            if (key) emit_key(ufd, key, je.value ? 1 : 0);
        }
        else if (je.type == JS_EVENT_AXIS) {
            /* D-pad: flechas direccionales (defaults Mednafen) */
            if (je.number == 6) {
                handle_axis(ufd, &axis_x_state, je.value, KEY_LEFT, KEY_RIGHT);
            } else if (je.number == 7) {
                handle_axis(ufd, &axis_y_state, je.value, KEY_UP, KEY_DOWN);
            }
        }
    }

    ioctl(ufd, UI_DEV_DESTROY);
    close(ufd);
    close(jfd);
    unlink(READY_FILE);
    return 0;
}
