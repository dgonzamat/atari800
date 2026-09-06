/* Puente entre libatari800 y el navegador.
   libatari800 es C puro (sin SDL): expone el framebuffer indexado y la
   inyeccion de teclas/joystick, que es todo lo que necesita un front-end
   de canvas. */
#include <string.h>
#include <emscripten.h>

#include "libatari800/libatari800.h"
#include "atari.h"
#include "memory.h"
#include "cassette.h"
#include "esc.h"

/* Colours_table[] vive en el core; 0x00RRGGBB por indice de color Atari. */
extern int Colours_table[256];

#define SCR_W 384
#define SCR_H 240

static input_template_t input;
static unsigned int rgba[SCR_W * SCR_H];   /* buffer que lee el canvas */

EMSCRIPTEN_KEEPALIVE
int a8_init(void)
{
    /* Maquina XL con la ROM real del usuario. Arranca sin disco: el menu
       elige cual montar via a8_load(), asi todos los juegos entran por el
       mismo camino. */
    char *argv[] = {
        "atari800",
        "-xlxe_rom", "/data/ATARIXL.ROM", "-xl-rev", "custom",
        "-osb_rom",  "/data/ATARIOSB.ROM", "-800-rev", "custom",
        "-nobasic",
        NULL,
    };
    int argc = (int) (sizeof(argv) / sizeof(argv[0])) - 1;

    if (!libatari800_init(argc, argv))
        return 0;
    /* Sin esto, un BRK aborta la emulacion via longjmp y varios cargadores
       de disco se quedan colgados a mitad de carga. */
    libatari800_continue_emulation_on_brk(1);
    libatari800_clear_input_array(&input);
    return 1;
}

EMSCRIPTEN_KEEPALIVE
void a8_frame(void)
{
    unsigned char *scr;
    int i;

    libatari800_next_frame(&input);

    /* La tecla la suelta quien la pulso, no este frame. Borrarla aqui la
       dejaba viva un solo frame y el teclado del Atari, que rebota durante
       varios, no llegaba a verla nunca: medido contra el menu de Arkanoid,
       ninguna tecla movia el cursor. La pagina la sostiene y la suelta. */

    scr = libatari800_get_screen_ptr();
    if (scr == NULL)
        return;
    for (i = 0; i < SCR_W * SCR_H; i++) {
        int c = Colours_table[scr[i]];
        /* canvas espera ABGR little-endian: 0xAABBGGRR */
        rgba[i] = 0xff000000u
                | (unsigned int) ((c & 0x0000ff) << 16)   /* B */
                | (unsigned int) (c & 0x00ff00)           /* G */
                | (unsigned int) ((c & 0xff0000) >> 16);  /* R */
    }
}

EMSCRIPTEN_KEEPALIVE unsigned int *a8_pixels(void) { return rgba; }
EMSCRIPTEN_KEEPALIVE int a8_width(void)  { return SCR_W; }
EMSCRIPTEN_KEEPALIVE int a8_height(void) { return SCR_H; }

/* joy: bitmask del joystick Atari (0=arriba,1=abajo,2=izq,3=der; activo en bajo
   lo maneja el core, aqui se pasa tal cual lo define libatari800). */
EMSCRIPTEN_KEEPALIVE
void a8_input(int joy, int trig, int keychar, int keycode,
              int start, int select, int option)
{
    input.joy0    = (unsigned char) joy;
    input.trig0   = (unsigned char) trig;
    input.keychar = (unsigned char) keychar;
    input.keycode = (unsigned char) keycode;
    input.start   = (unsigned char) start;
    input.select  = (unsigned char) select;
    input.option  = (unsigned char) option;
}

EMSCRIPTEN_KEEPALIVE
int a8_frame_number(void) { return libatari800_get_frame_number(); }

/* Posicion real del cabezal en la cinta montada, en bloques. Es lo unico
   honesto para mover una barra de progreso: cuenta lo que el cargador ha
   leido de verdad, no los frames que lleva corriendo el emulador. Con 0/0
   no hay cinta puesta. */
EMSCRIPTEN_KEEPALIVE
int a8_tape_pos(void)  { return CASSETTE_GetPosition(); }

EMSCRIPTEN_KEEPALIVE
int a8_tape_size(void) { return CASSETTE_GetSize(); }

/* --- que maquina se enciende --------------------------------------------
   Un 600XL, un 800XL, un 65XE y un 130XE son la MISMA maquina: misma placa,
   misma ROM, mismo procesador. Lo unico que las separa es cuanta RAM traen
   -16K, 64K, 64K y 128K-, asi que aqui basta con decir el tamano; el resto
   lo deduce el core (con 128K enciende el banco conmutado por PORTB que es
   lo que hace XE a un XE).

   Los tamanos legales para XL/XE no incluyen 48K: son 16, 64, 128, 192, 320,
   576 y 1088. Por eso el 48 es solo del Atari 800 de OS-B, y si llega un
   tamano que la maquina no admite se dice que no en vez de arrancar algo
   distinto de lo que se pidio. */
static int prepara(int machine, int ram)
{
    if (ram <= 0)                       /* 0 = la de fabrica */
        ram = (machine == Atari800_MACHINE_800) ? 48 : 64;
    if (!MEMORY_SizeValid(ram))
        return 0;
    if (machine == Atari800_machine_type && ram == MEMORY_ram_size)
        return 1;                       /* ya esta encendida esa */
    Atari800_SetMachineType(machine);
    MEMORY_ram_size = ram;
    return Atari800_InitialiseMachine();
}

/* Cuanta RAM tiene puesta ahora mismo, en KB. */
EMSCRIPTEN_KEEPALIVE
int a8_ram(void) { return MEMORY_ram_size; }

/* Cambia de juego en caliente: monta el fichero y arranca en frio.
   Devuelve el tipo de fichero detectado, o 0 si no se pudo abrir. */
/* Carga desde cassette. El parche SIO intercepta la rutina del OS y puentea
   la senal real de la cinta: con el activado el turbo loader nunca arranca,
   asi que hay que apagarlo antes del arranque en frio (y volver a encenderlo
   para los discos, donde acelera muchisimo la carga). */
EMSCRIPTEN_KEEPALIVE
int a8_load_tape(const char *path, int machine, int ram)
{
    if (!prepara(machine, ram))
        return 0;
    ESC_enable_sio_patch = FALSE;
    ESC_UpdatePatches();
    if (!CASSETTE_Insert(path))
        return 0;
    CASSETTE_hold_start = TRUE;      /* equivale a mantener START al encender */
    Atari800_Coldstart();
    libatari800_clear_input_array(&input);
    return 1;
}

/* machine: 0 = Atari 800 (OS-B), 1 = XL/XE. Algunos discos solo arrancan en
   una de las dos, asi que el catalogo lo indica por titulo. ram: los KB de
   la maquina elegida, o 0 para la de fabrica. */
EMSCRIPTEN_KEEPALIVE
int a8_load(const char *path, int machine, int ram)
{
    int t;
    if (!prepara(machine, ram))
        return 0;
    CASSETTE_Remove();
    ESC_enable_sio_patch = TRUE;     /* los discos si se benefician del parche */
    ESC_UpdatePatches();
    t = libatari800_reboot_with_file(path);
    libatari800_clear_input_array(&input);
    return t;
}

/* --- audio ---------------------------------------------------------------
   POKEY entrega PCM entero; el navegador quiere float [-1,1], asi que la
   conversion se hace aqui y JS solo copia un bloque contiguo. */
static float snd[16384];

EMSCRIPTEN_KEEPALIVE int a8_snd_freq(void)     { return libatari800_get_sound_frequency(); }
EMSCRIPTEN_KEEPALIVE int a8_snd_channels(void) { return libatari800_get_num_sound_channels(); }
EMSCRIPTEN_KEEPALIVE float *a8_snd_ptr(void)   { return snd; }

/* Convierte el buffer del frame actual y devuelve cuantas muestras (por canal
   ya intercaladas) quedaron disponibles en a8_snd_ptr(). */
EMSCRIPTEN_KEEPALIVE
int a8_snd_fill(void)
{
    unsigned char *buf = libatari800_get_sound_buffer();
    int len = libatari800_get_sound_buffer_len();
    int ssize = libatari800_get_sound_sample_size();
    int n, i;

    if (buf == NULL || len <= 0)
        return 0;

    n = len / ssize;
    if (n > (int) (sizeof(snd) / sizeof(snd[0])))
        n = (int) (sizeof(snd) / sizeof(snd[0]));

    if (ssize == 2) {
        signed short *p = (signed short *) buf;
        for (i = 0; i < n; i++)
            snd[i] = (float) p[i] / 32768.0f;
    }
    else {
        /* 8 bits sin signo, centrado en 128 */
        for (i = 0; i < n; i++)
            snd[i] = ((float) buf[i] - 128.0f) / 128.0f;
    }
    return n;
}
