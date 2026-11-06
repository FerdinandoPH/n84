# Análisis de viabilidad: emular TI-83+/84+ y TI-84 Plus CE en una TI-Nspire CX II

Fecha: 2026-11-06

## 1. Conclusión

**Es viable.** La TI-84 Plus/SE (Z80) se puede emular a velocidad completa con margen de sobra. La TI-84 Plus CE (eZ80) también es viable, aunque con más riesgo de rendimiento: lo esperable es que el TI-OS y la mayoría de programas vayan a una velocidad usable (≈60–100 %). Los juegos más exigentes podrían necesitar un núcleo optimizado (dispatch en ensamblador ARM o recompilación dinámica). Hay que medirlo en la fase 0 antes de comprometerse con objetivos de velocidad para la CE.

| | TI-83+/84+/84+SE | TI-84 Plus CE / 83 Premium CE |
|---|---|---|
| CPU emulada | Z80, 6/15 MHz | eZ80, 48 MHz (con estados de espera de flash) |
| Memoria emulada | 0,5–2 MB flash + 32–128 KB RAM | 4 MB flash + 256 KB RAM (incluye VRAM) |
| Pantalla | 96×64 monocromo (T6A04) → escalado ×3 | 320×240 16 bpp, **igual que la Nspire** |
| Coste relativo de CPU | Bajo (≈15 M ciclos Z80/s) | Alto (≈48 M ciclos/s, pero la flash lenta reduce las instrucciones reales a ~6–15 M/s) |
| Riesgo | Bajo | Medio (rendimiento y transferencia de ficheros) |

Plataforma anfitriona (Nspire CX II): ARM926EJ-S a 396 MHz, sin FPU, 64 MB de SDRAM, LCD 320×240 RGB565 y Ndless (SDK presente en `~/Ndless`, con soporte hasta el instalador 6.2).

Cálculo aproximado: un intérprete en C bien escrito consume ~25–40 ciclos ARM por instrucción emulada, es decir ~10–16 MIPS a 396 MHz. Eso basta para el Z80 a 15 MHz (~3–4 MIPS reales) y queda justo para el eZ80 de la CE. **Hay que validar estas cifras en la fase 0.**

## 2. El proyecto antiguo: `pbfy0/nspire-z80`

- Repositorio: https://github.com/pbfy0/nspire-z80. Último commit en 2019-01-28; publicado en Omnimaga en 2016. El repositorio **no tiene fichero de licencia**, así que no reutilizamos su código, solo lo usamos como referencia.
- Tamaño: unas 3.300 líneas de C más `drz80.s` (7.963 líneas, el núcleo Z80 DrZ80 escrito en ensamblador ARM, procedente de emuladores de GP2X).
- Emula la 84+, la 84+SE y la 84+CSE (detecta el modelo por el tamaño de la ROM).

### Arquitectura

1. **Núcleo DrZ80 (ensamblador ARM)**. Es rápido, pero no es portable ni se puede probar en un PC, y su licencia es dudosa.
2. **Mapeo de memoria con la MMU real del ARM.** Reserva el espacio virtual `0xE0000000` y reescribe la tabla de traducción L1 del sistema (`mmu_base[0xdff]`, `mmu_base[0xe00]`), con tablas de páginas *coarse* propias, el dominio 1 y el bit de protección ROM del CR1. Cada cambio de banco del Z80 (puertos 5/6/7) reescribe entradas de página e invalida la TLB. Además **sustituye el vector de *data abort*** (`0x30`) para capturar los accesos fuera de rango.
3. **Interrupciones propias.** Sustituye el vector IRQ (`0x38`), reprograma el VIC (`0xDC000000`) e **inserta ceros en el manejador SWI de Ndless** en un desplazamiento fijo (`swi_addr + 0xb0`).
4. **Control de velocidad** con el timer `0x900C0000` y `WFI`, esperando una IRQ.
5. **Pantalla:** escribe directamente en el controlador LCD y la paleta (`0xC0000200`).

### Por qué no arranca en la CX II (con evidencias)

| Problema | Evidencia |
|---|---|
| Usa los registros de la **PMU de la CX/CX clásica** en `0x900B0010/14/28` (interrupción de energía y tecla ON). En la CX II esa zona es el **ADC**, y la PMU "Aladdin" está en `0x90140000`. | Firebird `core/mem.c`: en `emulate_cx2`, `apb 0x0B → adc_cx2`, `0x14 → aladdin_pmu`. libndls `on_key_pressed.c`: la CX II lee `0x90140810 bit 8`, no `0x900B0028 bit 4`. |
| **Parchea el manejador SWI de Ndless** en un desplazamiento fijo (+0xb0). El manejador actual (`ndless/src/resources/ints.c`) no es el de 2016, así que se pisan instrucciones arbitrarias y fallan las llamadas al sistema. | `interrupt.c: patch_ndless_swi()` |
| Secuestra los vectores IRQ y *abort* y modifica la tabla de la MMU del OS. Esto depende de la distribución de memoria del OS 3.x/4.x; en el OS 5.x puede colisionar o colgarse. | `mmu_mmap.c: mmu_init()`, `mprotect_init()` |
| El control de velocidad espera con `WFI` a una IRQ del timer. Si la IRQ no llega (mapeo distinto en el VIC o la PMU), **se cuelga para siempre**, que es exactamente el síntoma de "no enciende". | `speedcontrol.c` |
| Escribe directamente en el LCD. Ndless ya no expone `SCREEN_BASE_ADDRESS` y obliga a usar `lcd_init`/`lcd_blit`, porque la CX II y las revisiones HW-W tienen paneles distintos. | `libndls.h:165` |
| Está compilado con un SDK de la época (2016–2019) y nunca se probó en una CX II. | — |

**Lecciones:** el enfoque "tomar el control del hardware" es rápido, pero se rompe con cada revisión de hardware o de OS. El nuevo proyecto debe ser **poco intrusivo**: usar las APIs de Ndless y no tocar ni vectores ni la MMU, al menos mientras no se demuestre que hace falta. También debe tener un **núcleo portable** que se pueda probar en un PC.

## 3. Otros emuladores de referencia

| Proyecto | Lenguaje / licencia | Qué aporta |
|---|---|---|
| **CEmu** (CE-Programming/CEmu) | C, **GPLv3** | Referencia principal para la CE. `core/` tiene unas 10.000 líneas: `cpu.c` (eZ80), `mem.c`, `flash.c`, `lcd.c`, `panel.c`, `keypad.c`, `timers.c`, `realclock.c`, `sha256.c`, `usb/` (DUSB y FOTG210). Es **exacto al ciclo**, con un *scheduler* de 64 bits, emulación del panel por píxel y curvas gamma en `float`, así que es **demasiado pesado para portarlo tal cual a un ARM926 sin FPU**. Lo usaremos como especificación ejecutable y como **oráculo**: comparar trazas instrucción a instrucción en el PC. |
| **z80e** (KnightOS) | C, **MIT** | Núcleo Z80 limpio (~1.500 líneas) y emulación de 83+/84+ (puertos, T6A04, bancos). Sirve de base o de referencia para el núcleo Z80. |
| **TilEm** | C, GPL | Muy completo para la familia Z80: protocolo de enlace, escala de grises, todos los modelos. Es la mejor referencia para el enlace y las variables. |
| **Firebird** (nspire-emus) | C/C++, GPLv3 | Emulador de la propia Nspire. Es nuestra herramienta de pruebas (stub gdb) y documenta el mapa de hardware de la CX II. |
| **Calcemu** (Jacobly, 2013) | Cerrado | Demostró que un 84+ funciona bien en una Nspire CX con Ndless. Hoy ya no funciona. |
| Emulador 84+ oficial de TI | — | Las Nspire clásicas con teclado de 84 lo traían en su firmware. No existe en la CX II. |

## 4. Decisiones que se derivan del análisis

1. **Núcleos de CPU en C99 portable**, que se compilen tanto para el PC (pruebas, trazas, comparación con CEmu) como para la Nspire. Las rutas críticas se optimizan después (dispatch con *computed goto*, ensamblador ARM o JIT) sin cambiar la interfaz.
2. **Memoria emulada mediante tablas de punteros por página**, sin MMU: 16 KB por banco en el Z80 y 64 KB o 4 KB en los 24 bits del eZ80. El truco de la MMU queda como optimización opcional, muy posterior y detrás de una opción.
3. **Uso exclusivo de las APIs de Ndless** para pantalla (`lcd_init`/`lcd_blit`), teclado (`isKeyPressed`/`keypad`), ficheros (stdio) y tiempo. Sin IRQ propias. El control de velocidad se hace por sondeo.
4. **Emulación de alto nivel donde no importa la exactitud:** el LCD se dibuja por fotogramas (no por píxel), el panel SPI de la CE se trata de forma mínima y el USB y la linkport se sustituyen por inyección de variables mediante llamadas al propio OS emulado.
5. **Licencia GPLv3**, compatible con estudiar y adaptar CEmu (GPLv3) y z80e (MIT).

## 5. Riesgos principales

| Riesgo | Mitigación |
|---|---|
| La CE no llega a velocidad completa | Medirlo en la fase 0. Por orden: dispatch optimizado, caché de páginas, rutas en ensamblador y JIT opcional. Avisar en la interfaz si la velocidad baja del 100 %. |
| Transferir variables a la CE (solo tiene USB) | Inyección HLE con rutinas del OS. Como alternativa, portar el DUSB de CEmu. |
| ROMs con copyright | El usuario vuelca la ROM de su propia calculadora. Para la 84+ existe la alternativa de la boot libre (BootFree) más un `.8xu` oficial que se puede descargar gratis. |
| El OS de la CE ≥ 5.5 bloquea el ensamblador | Documentarlo: usar un OS ≤ 5.4 o arTIfiCE/asmhook, igual que en una CE real. |
| Variantes de panel en la CX II (320×240 frente a 240×320) | Usar `lcd_blit` de Ndless, que ya gestiona la rotación. |
| Memoria disponible bajo Ndless | Hace falta < 8 MB en total (4 MB de flash CE, 256 KB de RAM, búferes). Comprobar `malloc` grande en la fase 0. |

## Fuentes

- https://github.com/pbfy0/nspire-z80
- https://www.omnimaga.org/ti-nspire-projects/nspire-z80-open-source-ti-84-emulator-for-the-nspire-cx
- https://omnimaga.org/ti-nspire-projects/z80-emulator-for-nspire/195 (Calcemu)
- https://www.hackspire.org/TI-84_Plus_Emulation/
- https://github.com/CE-Programming/CEmu
- https://github.com/KnightOS/z80e
- https://github.com/nspire-emus/firebird
