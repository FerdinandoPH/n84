# Requisitos para desarrollar N84

Lo que necesito (Claude) para trabajar en el proyecto y lo que ya está comprobado en este equipo. Las marcas indican: ✅ comprobado · ⚠️ falta confirmarlo · ❌ falta.

## 1. Entorno y herramientas

| Elemento | Estado | Detalle |
|---|---|---|
| Ndless SDK | ✅ | `~/Ndless/ndless-sdk`: `nspire-gcc` (arm-none-eabi-gcc 14.2.0), `genzehn`, `make-prg`; commit c168cae (2025-12-19) |
| GDB para ARM | ✅ | `~/Ndless/ndless-sdk/toolchain/install/bin/arm-none-eabi-gdb` |
| Compilador del host | ✅ | gcc 13.3 (WSL Ubuntu 24.04) |
| SDL2 en el host | ✅ | 2.30.0 (para una ventana de depuración opcional) |
| git | ✅ | El directorio del proyecto todavía **no** es un repo git. Hay que inicializarlo en la fase 0. |
| **Firebird** | ⚠️ | **No está en el PATH de WSL.** Necesito saber dónde está (¿Windows o Linux?), cómo se lanza, el **puerto del stub gdb** y si puedo acceder a él desde WSL (`localhost` o la IP del host Windows). |
| Imagen de Firebird | ⚠️ | Una *flash image* de la CX II (CAS) con el OS 5.3 **y Ndless ya instalado**, y un snapshot guardado después del arranque para no repetir el boot en cada prueba |
| Envío de `.tns` a Firebird | ⚠️ | ¿Lo haces a mano (arrastrar y soltar) o hay alguna forma automatizable? Es lo que más condiciona la velocidad de iteración. |
| TiLP o TI Connect CE | ⚠️ | Para pasar binarios a la calculadora real y para volcar ROMs |
| CEmu compilado en el host | ❌ | Lo compilo yo (el núcleo es C puro, sin Qt) como **oráculo** para la fase 5. Solo hace falta permiso para clonarlo y compilarlo. |

## 2. Información sobre tu hardware

- Versión **exacta** del OS de la Nspire (p. ej. 5.3.0.564), revisión de hardware (en *Acerca de*, o bien `hwtype`/`nl_hwsubtype`) y si es CX II CAS, CX II-T CAS, etc.
- Versión de Ndless instalada en la calculadora.
- Si el **panel** es 320×240 o 240×320. Puedo detectarlo con un programa de prueba en la fase 0.
- ¿Tienes **físicamente** una TI-84 Plus / SE, una 83+ o una TI-84 Plus CE? Esto determina de dónde salen las ROMs (sección 3).

## 3. ROMs y sistemas operativos (imprescindible)

No puedo descargar ni distribuir ROMs de TI. Necesito que me las proporciones en una carpeta local, por ejemplo `~/0_progra/calc/roms/`, **fuera del repo y en `.gitignore`**.

| Uso | Fichero | Cómo conseguirlo |
|---|---|---|
| 84+SE (principal Z80) | ROM de 2 MB | Volcado de tu calculadora (TiLP + *rom dumper*). **Alternativa legal sin calculadora:** boot libre (BootFree o la de TilEm) + `TI84Plus_OS255.8xu`, que se descarga gratis en education.ti.com; TilEm/`rom8x` construyen la imagen. |
| 84+ / 83+ / 83+SE | ROM de 1 MB / 512 KB / 2 MB | Igual que la anterior. Deseable, no imprescindible al principio. |
| TI-84 Plus CE | ROM de 4 MB (boot + OS) | **Requiere una CE física:** ROMDumper de CE-Programming, el mismo método que usa CEmu. No hay alternativa libre para la boot de la CE. |
| Versión de OS de la CE | — | Preferiblemente **≤ 5.4**, para que el ensamblador funcione sin jailbreak, o bien 5.6+ junto con arTIfiCE. Indica cuál tienes. |

> Si no tienes una CE física, la parte CE (fases 5–8) **no se puede probar con el OS real**. Podríamos validar el núcleo eZ80 con programas de prueba propios, pero no el producto final.

## 4. Software de prueba

Te pediré que descargues (o me autorices a descargar) desde ticalc.org o los repos oficiales:

- **Z80:** `zexdoc`/`zexall` (versión CP/M, para el host), Doors CS 7, MirageOS, Phoenix, un juego en escala de grises (p. ej. Desolate), un programa BASIC de prueba propio.
- **CE:** Cesium, las librerías CE (`clibs.8xg`), 2 o 3 programas C del CE Toolchain (uno sencillo y uno exigente para el benchmark), un programa en ASM.
- Opcional: el CE C/C++ Toolchain instalado en el host, para generar programas de prueba a medida.

## 5. Ciclo de trabajo y pruebas

- **Host (autónomo):** compilo y ejecuto los tests en Linux sin ayuda: ZEXDOC, arranque de ROM con capturas PPM y comparación con CEmu.
- **Firebird:** ideal si puedo lanzarlo y controlarlo con gdb desde WSL. Si no, te pediré que cargues el `.tns` y me pases lo que salga (capturas o el log de `nspireio`).
- **Calculadora real:** imprescindible para **medir rendimiento**, porque Firebird no reproduce la velocidad real. Necesitaré que ejecutes el benchmark y me pases las cifras en las fases 0, 3 y 7.
- **Logs:** en la Nspire usaré la salida serie (visible en la consola de Firebird) o un fichero `n84.log.tns`.

## 6. Decisiones que necesito de ti

1. Licencia: **GPLv3** (recomendada; permite adaptar CEmu).
2. Orden: **Z80 primero y después CE** (recomendado) o al revés.
3. Nombre del proyecto (provisional: N84) y si se publicará en GitHub.
4. Idioma: código y comentarios en inglés y documentación en español (recomendado), o todo en español.
5. Permiso para inicializar git en `~/0_progra/calc/new-nspire-z80` y para clonar repos de referencia (CEmu, z80e, TilEm) en una carpeta `third_party/` o fuera del repo.

## 7. Documentación de referencia (la consulto yo)

- Hackspire (hardware de la Nspire y CX II), WikiTI (puertos y hardware de 83+/84+, bcalls, formato `.8x?`), wiki de CE-Programming y `ti84pceg.inc` (hardware y OS de la CE, puertos y MMIO).
- Código: CEmu `core/`, z80e, TilEm y Firebird `core/cx2.cpp` y `core/mem.c`.
