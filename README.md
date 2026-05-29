# Consola de Videojuegos Retro

Centro de entretenimiento con emulación de NES, SNES y Game Boy Advance sobre
**Raspberry Pi 4** (AArch64), construido desde cero generado con **Buildroot**.

Proyecto final de la asignatura **Fundamentos de Sistemas Embebidos**.

## Características

- Arranque directo a galería de juegos, sin escritorio ni gestor de ventanas.
- Splash personalizado (imagen + sonido) durante el arranque.
- Interfaz limpia controlada con gamepad (DualSense USB).
- Emulación con **Mednafen** (NES + SNES + GBA).
- **Importación inteligente desde USB**: al insertar una memoria con ROMs,
  el sistema pausa la galería, monta el USB, copia las ROMs nuevas evitando
  duplicados por hash SHA-1, y actualiza la lista sin reiniciar.
- Sistema construido íntegramente desde la base.
