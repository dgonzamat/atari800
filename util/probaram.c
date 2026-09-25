/* Enciende una maquina XL/XE con la RAM que se le diga y mete un juego.

   Es el mismo camino que usa a8web.c en el navegador -SetMachineType,
   MEMORY_ram_size, InitialiseMachine- asi que sirve para dos cosas: probar
   que el cambio del puente es correcto, y averiguar de verdad que juegos
   entran en los 16K de un 600XL en vez de suponerlo por la direccion mas
   alta del fichero (que miente: la pantalla vive arriba de todo la RAM y
   se come lo ultimo).

   Uso:  probaram <fichero> <KB> [segundos]
   Escribe pantalla.pgm y dice cuanta pantalla quedo escrita.
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "libatari800.h"
#include "atari.h"
#include "cassette.h"
#include "esc.h"
#include "memory.h"

int main(int argc, char **argv)
{
	char *args[] = { "-xl", "-nobasic", "-xlxe_rom", "ATARIXL.ROM", NULL };
	input_template_t in;
	const char *fich = argc > 1 ? argv[1] : NULL;
	int kb   = argc > 2 ? atoi(argv[2]) : 64;
	int segs = argc > 3 ? atoi(argv[3]) : 8;
	int f, i, distintos = 0, tinta = 0;
	unsigned char *scr;
	int visto[256];

	if (fich == NULL) { fprintf(stderr, "uso: probaram <fichero> <KB> [seg]\n"); return 2; }

	if (!libatari800_init(4, args)) { fprintf(stderr, "no arranco\n"); return 1; }
	/* Igual que a8web.c en el navegador: un BRK no aborta la emulacion. Sin
	   esto el banco de pruebas no mide lo que la pagina hace de verdad. */
	libatari800_continue_emulation_on_brk(1);

	if (!MEMORY_SizeValid(kb)) { fprintf(stderr, "%d KB no es un tamano legal\n", kb); return 2; }
	Atari800_SetMachineType(Atari800_MACHINE_XLXE);
	MEMORY_ram_size = kb;
	if (!Atari800_InitialiseMachine()) { fprintf(stderr, "no se pudo armar con %dK\n", kb); return 1; }

	/* La cinta va por otro camino, el mismo que la pagina: sin parche SIO
	   -si no, el turbo loader nunca arranca- y con START mantenido. */
	if (strstr(fich, ".cas") != NULL) {
		ESC_enable_sio_patch = FALSE;
		ESC_UpdatePatches();
		if (!CASSETTE_Insert((char *) fich)) { printf("NOMONTA\n"); return 0; }
		CASSETTE_hold_start = TRUE;
		Atari800_Coldstart();
	}
	else {
		CASSETTE_Remove();
		ESC_enable_sio_patch = TRUE;
		ESC_UpdatePatches();
		if (!libatari800_reboot_with_file((char *) fich)) {
			printf("NOMONTA\n");
			return 0;
		}
	}
	libatari800_clear_input_array(&in);
	for (f = 0; f < segs * 50; f++)
		libatari800_next_frame(&in);
	/* y el START que lanza lo que la cinta dejo en memoria */
	if (strstr(fich, ".cas") != NULL) {
		in.start = 1;
		for (f = 0; f < 60; f++) libatari800_next_frame(&in);
		libatari800_clear_input_array(&in);
		for (f = 0; f < 300; f++) libatari800_next_frame(&in);
	}

	/* Cuanta pantalla quedo escrita: cuantos colores distintos hay y que
	   parte no es el color de fondo. Una maquina que no pudo con el juego
	   deja la pantalla de un solo color, o el azul del sistema. */
	scr = libatari800_get_screen_ptr();
	memset(visto, 0, sizeof(visto));
	for (i = 0; i < 384 * 240; i++)
		visto[scr[i]]++;
	for (i = 0; i < 256; i++)
		if (visto[i]) distintos++;
	{
		int fondo = 0, max = 0;
		for (i = 0; i < 256; i++)
			if (visto[i] > max) { max = visto[i]; fondo = i; }
		tinta = (384 * 240 - visto[fondo]) * 1000 / (384 * 240);
	}
	printf("colores=%d tinta=%d/1000\n", distintos, tinta);
	{
		FILE *o = fopen("pantalla.pgm", "wb");
		fprintf(o, "P5\n384 240\n255\n");
		fwrite(scr, 1, 384 * 240, o);
		fclose(o);
	}
	libatari800_exit();
	return 0;
}
