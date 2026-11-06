# Especificación: N84, emulador de TI-83+/84+ y TI-84 Plus CE para TI-Nspire CX II

Nombre provisional: **N84**. Versión del documento: 0.1 (2026-11-06). La justificación técnica de las decisiones está en [ANALISIS.md](ANALISIS.md).

---

## 1. Objetivo

Crear un programa Ndless para la TI-Nspire CX II (CAS) que ejecute el software de:

- **Familia Z80:** TI-83 Plus, TI-83 Plus SE, TI-84 Plus y TI-84 Plus SE. Opcionales: TI-84 Plus C SE y TI-83 clásica.
- **Familia eZ80:** TI-84 Plus CE, TI-84 Plus CE-T y TI-83 Premium CE (mismo hardware). La parte Python de la "Python Edition" (coprocesador ARM) queda fuera del alcance.

El emulador ejecuta el **TI-OS original** a partir de una ROM proporcionada por el usuario. Con él se pueden usar programas en BASIC y ensamblador, aplicaciones Flash y shells como Doors CS, MirageOS, Cesium o arTIfiCE.

### Fuera del alcance (v1)

- Emular la linkport o el USB contra calculadoras físicas.
- La pila Python de la CE Python Edition.
- Exactitud al ciclo, más allá de lo que haga falta para que el software funcione.
- Otros modelos de Nspire. El objetivo es la CX II; la CX clásica es *best effort* si sale gratis.

---

## 2. Requisitos funcionales

| ID | Requisito | Prioridad |
|---|---|---|
| RF-01 | Abrir una imagen ROM (`*.rom.tns`) desde el explorador de documentos y detectar el modelo por tamaño y contenido. | Imprescindible |
| RF-02 | Arrancar el TI-OS hasta la pantalla principal y usarlo con normalidad (cálculo, gráficas, menús). | Imprescindible |
| RF-03 | Mostrar la pantalla emulada: escalada ×3 y centrada en las Z80; 1:1 a pantalla completa en la CE. | Imprescindible |
| RF-04 | Mapear el teclado de la Nspire al teclado de la 84 (mapa por defecto y mapa configurable). | Imprescindible |
| RF-05 | Limitar la velocidad a la nominal (6/15/48 MHz) y ofrecer un modo turbo sin límite. | Imprescindible |
| RF-06 | Menú del emulador (tecla reservada, por defecto `doc`/`menu` + combinación): continuar, guardar o cargar estado, enviar fichero, turbo, opciones, salir. | Imprescindible |
| RF-07 | Guardar estado al salir y restaurarlo al volver a abrir (`*.sav.tns`). Varias ranuras. | Imprescindible |
| RF-08 | Persistir los cambios en la flash emulada (Archive) de vuelta a la ROM, o a un fichero aparte `*.flash.tns`. | Imprescindible |
| RF-09 | Cargar variables y apps (`.8xp .8xv .8xg .8xl .8xm .8xs .8xk .8ck .8xu`, renombradas con `.tns`) en el modelo emulado: en RAM o en Archive. | Imprescindible |
| RF-10 | Asociar extensiones (`cfg_register_fileext`) para poder abrir un `.8xp.tns` directamente y que lo envíe a la última ROM usada. | Deseable |
| RF-11 | Escala de grises en las Z80 mediante mezcla temporal de fotogramas. | Deseable |
| RF-12 | Indicador opcional de velocidad (FPS y % de velocidad real). | Deseable |
| RF-13 | Extraer variables de la calculadora emulada a ficheros `.8x?.tns`. | Deseable |
| RF-14 | TI-84 Plus C SE (Z80 + LCD a color ILI9335). | Opcional |
| RF-15 | Configuración en `n84.cfg.tns`: mapa de teclas, escala, filtros, ranura por defecto, tecla del menú. | Deseable |

## 3. Requisitos no funcionales

| ID | Requisito |
|---|---|
| RNF-01 | **Rendimiento:** familia Z80 al 100 % de la velocidad real (15 MHz) con al menos un 50 % de CPU libre. CE: **objetivo mínimo del 60 % y objetivo final del 100 %** en el TI-OS y en programas C del toolchain CE, a medir con los benchmarks de la sección 9. |
| RNF-02 | **No intrusivo:** sin modificar vectores de excepción, tablas de la MMU, el VIC ni la PMU del OS. Solo APIs de Ndless y periféricos documentados que se restauren al salir. Al pulsar "salir", la Nspire vuelve al OS en un estado limpio. |
| RNF-03 | **Portabilidad del núcleo:** CPU y máquinas en C99, sin dependencias de Ndless, compilables en Linux x86-64 para pruebas. |
| RNF-04 | **Sin coma flotante** en el bucle principal: el ARM926 no tiene FPU. |
| RNF-05 | **Memoria:** menos de 12 MB de pico en la CE. |
| RNF-06 | **Arranque:** la ROM de la CE (4 MB) se carga en menos de 3 s desde la flash de la Nspire. |
| RNF-07 | **Robustez:** las ROMs corruptas, los ficheros inválidos y la falta de memoria muestran un mensaje y nunca cuelgan. |
| RNF-08 | **Licencia GPLv3.** Ningún binario de TI distribuido con el proyecto. |

---

## 4. Arquitectura

```
┌──────────────────────────────────────────────────────────────┐
│ frontend/      menú, config, savestates, carga de ficheros   │
├──────────────────────────────────────────────────────────────┤
│ machine/ti8x/  (Z80)          │ machine/ti84ce/  (eZ80)      │
│  mem+bancos, puertos, T6A04,  │  bus 24 bits, flash, RAM,    │
│  timers, teclado, RTC, MD5,   │  LCD PL111, panel SPI mín.,  │
│  flash cmd, link HLE          │  teclado, GPT, RTC, SHA256,  │
│                               │  ctl interrupciones, USB stub│
├───────────────────────────────┼──────────────────────────────┤
│ cpu/z80                       │ cpu/ez80                     │
├──────────────────────────────────────────────────────────────┤
│ platform/  (interfaz única: video, input, time, fs, log)     │
│   ├─ nspire/   Ndless: lcd_blit, isKeyPressed, timer, stdio  │
│   └─ host/     Linux: headless (PPM/trazas) y SDL2 opcional  │
└──────────────────────────────────────────────────────────────┘
```

### 4.1 Estructura del repositorio

```
src/cpu/z80/        z80.c z80.h
src/cpu/ez80/       ez80.c ez80.h
src/machine/ti8x/   ti8x.c mem.c ports.c lcd_t6a04.c timers.c keypad.c flash.c link_hle.c
src/machine/ti84ce/ ce.c bus.c flash.c lcd.c keypad.c timers.c rtc.c intc.c sha256.c spi.c control.c vars_hle.c
src/frontend/       main.c menu.c config.c savestate.c files.c
src/platform/       platform.h  nspire/*.c  host/*.c
tests/              cpu tests, ROM-boot tests, comparaciones con CEmu
tools/              scripts (trazas, despliegue a Firebird, benchmark)
docs/
Makefile            make nspire | make host | make test
```

### 4.2 Interfaz de plataforma (`platform.h`)

```c
// video
bool  plat_video_init(void);                // lcd_init(SCR_320x240_565)
uint16_t *plat_video_backbuffer(void);      // 320x240 RGB565
void  plat_video_present(void);             // lcd_blit
// entrada
void  plat_input_poll(plat_keys_t *out);    // bitmap de teclas Nspire
bool  plat_on_pressed(void);
// tiempo
uint32_t plat_time_us(void);                // contador monotónico (sondeo)
void  plat_idle_until(uint32_t t_us);       // espera activa o idle sin IRQ propia
// ficheros, mensajes, log
FILE *plat_fopen(const char *path, const char *mode);
void  plat_msgbox(const char *title, const char *msg);
void  plat_log(const char *fmt, ...);       // nspireio / serie / stdout en host
```

### 4.3 Bucle principal

```
mientras !salir:
    presupuesto = ciclos_hasta(próximo_evento)        // timer, VBlank, 1/60 s de input
    cpu_run(presupuesto)                              // ejecuta hasta agotar o IRQ/HALT
    machine_advance(ciclos_ejecutados)                // timers, interrupciones
    si toca fotograma (≈60 Hz o VBlank de la CE):
        input_poll → matriz de teclado emulada
        render (solo si cambió la VRAM o el LCD)       → plat_video_present
        pacing: si vamos adelantados, plat_idle_until  // sin WFI dependiente de IRQ
```

- Las interrupciones y el HALT se gestionan dentro de `cpu_run`. Durante un HALT con las interrupciones habilitadas se avanza directamente al próximo evento, lo que ahorra mucha CPU en el TI-OS.
- El *scheduler* es ligero: un array pequeño de eventos con contadores de 32 bits relativos. No se usa el *scheduler* de 64 bits de CEmu.

---

## 5. Núcleos de CPU

### 5.1 Requisitos comunes

- Intérprete con `switch` o con tabla de *computed goto* (GCC). Registros en una `struct` y los más usados en variables locales dentro del bucle de ejecución.
- **Acceso a memoria rápido:** `read_page[addr >> SHIFT]` es un puntero directo o NULL, y NULL desvía al manejador lento (MMIO, flash en modo comando, zonas protegidas). Igual para la escritura: las páginas de flash son NULL en escritura, de modo que cualquier escritura pasa por el manejador de comandos de flash.
- Fetch de instrucciones con un puntero a la página actual cacheado, que se invalida al cambiar de banco o de página.
- Contabilidad de ciclos por instrucción con tabla. En la CE se suman los estados de espera de flash y RAM de forma aproximada, con una media por región.
- API común:

```c
void     cpu_reset(cpu_t*);
int32_t  cpu_run(cpu_t*, int32_t cycles);   // devuelve ciclos ejecutados
void     cpu_irq(cpu_t*, bool level);  void cpu_nmi(cpu_t*);
void     cpu_save(cpu_t*, stream*); void cpu_load(cpu_t*, stream*);
```

### 5.2 Z80

- Juego de instrucciones completo, incluidas las no documentadas que usan los juegos (IXH/IXL, SLL, flags X/Y opcionales), IM 1/2 y R.
- Criterio de aceptación: pasar **ZEXDOC** (obligatorio) y **ZEXALL** (deseable) en el host.

### 5.3 eZ80

- Modos **ADL** y **Z80** con MBASE, sufijos `.SIS/.LIS/.SIL/.LIL`, `LEA`, `PEA`, `MLT`, `TST`, `IN0/OUT0`, `SLP`, `STMIX/RSMIX`, `MADL`, pilas SPS/SPL, `IM 2` con vectores de 24 bits e instrucciones de bloque de E/S.
- Criterio de aceptación: **comparación en paralelo con CEmu**. Ambos arrancan la misma ROM y se comparan los registros cada N instrucciones durante los primeros 50 M de instrucciones del arranque del OS y de una batería de programas, sin divergencias. Herramienta: `tools/trace_diff`.

### 5.4 Optimización (después de que funcione)

Por orden, midiendo en cada paso:

1. *computed goto* y registros locales.
2. Especializar los opcodes ADL frente a Z80 en tablas separadas.
3. Fusionar las secuencias de instrucciones más frecuentes del OS (perfil con trazas).
4. Rutas críticas en ensamblador ARM.
5. **Opcional:** JIT por bloques básicos a ARM, solo si la CE no llega al objetivo.

---

## 6. Máquinas

### 6.1 Familia Z80 (`machine/ti8x`)

| Modelo | Flash | RAM | CPU | Identificación |
|---|---|---|---|---|
| 83+ | 512 KB | 32 KB | 6 MHz | ROM de 512 KB |
| 83+SE | 2 MB | 128 KB | 6/15 MHz | ROM de 2 MB + boot 83+SE |
| 84+ | 1 MB | 128 KB | 6/15 MHz | ROM de 1 MB |
| 84+SE | 2 MB | 128 KB | 6/15 MHz | ROM de 2 MB + boot 84+ |
| 84+CSE (opcional) | 4 MB | 128 KB | 6/15 MHz | ROM de 4 MB con boot CSE |

Hardware que hay que emular (referencia: WikiTI, z80e y TilEm):

- **Memoria y bancos:** puertos 04/05/06/07/0E/0F, modos de mapeo 0 y 1, página de boot en el arranque y protección de ejecución (puertos 21–27).
- **Flash:** protocolo de comandos AMD (programar, borrar sector), bloqueo del puerto 14 y **escritura de vuelta** al guardar.
- **LCD T6A04** (puertos 10/11): modos 6 y 8 bits, autoincremento, contraste y desplazamiento Z. Render a 96×64 → ×3 → 288×192 centrado, con paleta según el contraste y mezcla opcional para grises (RF-11).
- **Interrupciones:** puertos 03/04 (ON, timers hardware 1/2, link, temporizadores de cristal 30–38).
- **Teclado:** puerto 01, matriz de 7×8.
- **Otros:** reloj (puertos 40–47), MD5 (18–1D), puerto 20 (velocidad), puerto 02/15 (estado e ID de ASIC), puerto 00 (linkport en reposo) y USB (puertos 4x/5x) como stub "desconectado".

### 6.2 Familia CE (`machine/ti84ce`)

Mapa de memoria de 24 bits: flash 4 MB en `0x000000`, RAM 256 KB en `0xD00000` (VRAM a partir de `0xD40000`) y MMIO en `0xE00000–0xFFFFFF`. Puertos de E/S de 16 bits por bloques (`0x0xxx` control, `0x1xxx` flash, `0x2xxx` SHA256, `0x3xxx` USB, `0x4xxx` LCD, `0x5xxx` interrupciones, `0x6xxx` watchdog, `0x7xxx` GPT, `0x8xxx` RTC, `0x9xxx` protección, `0xAxxx` teclado, `0xBxxx` retroiluminación, `0xDxxx` SPI). Referencia: `CEmu/core/port.c`, `mem.c` y `asic.c`.

- **Flash:** lectura directa, comandos y borrado de sector, controlador de flash (estados de espera) y protección de rangos privilegiados. Implementar **lo que el boot y el OS necesiten** y dejar el resto como stubs que registren el acceso.
- **LCD PL111:** se lee UPBASE, el modo (bpp 1/2/4/8/16, 565/555/BGR) y la paleta. Render por fotograma de VRAM a RGB565 de la Nspire. **Ruta rápida:** si el modo es 16 bpp 565 sin rotación, se hace `lcd_blit` directo desde la RAM emulada, sin conversión. Se genera la interrupción de VBlank o LNBU que esperan los programas (double buffering de los juegos C).
- **Panel SPI (ST7789):** solo se almacenan los comandos. Se respeta la orientación o inversión básica si algún programa la usa (por ejemplo, el *trick* de 240×320 o la mitad de resolución), pero no se emulan la gamma ni los tiempos.
- **Teclado:** controlador de escaneo con modos continuo y único, registros de datos por fila e interrupción.
- **GPT** (3 timers), **RTC**, **controlador de interrupciones**, **watchdog** (stub), **SHA256** (la necesita el boot), **retroiluminación** (registro sin efecto real o mapeada al brillo de la Nspire si es seguro), **USB** (stub "sin cable"), **control de energía y velocidad** (puerto 0x01: 6/12/24/48 MHz).
- **Revisiones de hardware:** emular la revisión que espere la ROM del usuario. CEmu tiene la lógica de detección (`bootver.c`).

---

## 7. Transferencia de ficheros (RF-09, RF-13)

**Método principal: inyección HLE.** Con el emulador en pausa:

1. Se analiza el fichero `.8x?` (cabecera, entradas y checksum).
2. Se guarda el estado de la CPU, se coloca un *stub* en una zona segura de RAM (en la CE, `saveSScreen`/`safe_ram`; en la 84, `appBackUpScreen`) que llama a las rutinas del OS (84: `bcall _ChkFindSym/_DelVarArc/_CreateVar/_Arc_Unarc`; CE: entradas equivalentes de la tabla de saltos del OS), se copian los datos y se ejecuta hasta un *breakpoint* de retorno.
3. Se restaura el estado y se refresca la pantalla.

- Las apps Flash (`.8xk/.8ck`) y los OS (`.8xu/.8eu`) se escriben directamente en flash siguiendo el formato de cabeceras de la ROM. Esto es más complejo y va en una fase posterior.
- **Método alternativo** si la HLE da problemas con alguna versión de OS: emular un par de enlace virtual (protocolo DBUS sobre el puerto 00) en la 84 y portar `usb/dusb.c` y FOTG210 de CEmu en la CE.
- **Exportación:** se recorre la VAT (formato documentado; referencia: `CEmu/core/vat.c`) y se escribe el fichero `.8x?` con su checksum.

---

## 8. Frontend y formatos de fichero

| Fichero | Contenido |
|---|---|
| `NOMBRE.rom.tns` | Volcado de la ROM (512 KB, 1, 2 o 4 MB) |
| `NOMBRE.sav.tns` / `NOMBRE.s1.tns`… | Estado: cabecera `N84S`, versión, modelo, hash corto de la ROM, CPU, máquina y RAM. La flash solo se guarda si hay cambios, como diff por sectores. |
| `n84.cfg.tns` | Texto `clave=valor` |
| `*.8xp.tns` etc. | Variables para enviar |

- **Menú del emulador:** superpuesto en la pantalla de la Nspire con la fuente de Ndless o una propia de 8×8. Se navega con las flechas y `enter`.
- **Mapa de teclado por defecto:** las teclas con la misma serigrafía se corresponden; las demás se asignan a `ctrl/shift` + tecla y se documentan en una tabla dentro del README.
- **Salir:** guarda el estado automáticamente (configurable), escribe la flash si cambió, restaura el LCD con `lcd_init(SCR_TYPE_INVALID)` y vuelve al OS.

---

## 9. Pruebas y benchmarks

| Nivel | Qué | Dónde |
|---|---|---|
| Unitario CPU | ZEXDOC/ZEXALL (Z80) y suite propia de opcodes eZ80 | Host, `make test` |
| Oráculo | Comparación de trazas eZ80 frente a CEmu, y Z80 frente a z80e o TilEm | Host |
| Arranque | ROM → pantalla principal en N fotogramas; captura PPM comparada con una imagen de referencia | Host |
| Integración | Ejecutar `.tns` en Firebird (CX II, OS 5.3 + Ndless) con depuración por gdb | PC |
| Hardware real | Batería manual en la CX II del usuario | Calculadora |
| Rendimiento | `bench`: MIPS del núcleo con un bucle sintético; % de velocidad en el arranque del OS, en el gráfico de `Y=sin(X)` y en un juego C de la CE (pendiente de elegir) | Calculadora real; Firebird **no** sirve para medir velocidad |

Programas de prueba de referencia: Doors CS 7, Phoenix, MirageOS (Z80); Cesium y una demo del toolchain CE (CE C/C++ Toolchain) para la CE. La lista definitiva está en `REQUISITOS.md`.

---

## 10. Plan por fases e hitos

| Fase | Contenido | Criterio de salida |
|---|---|---|
| **F0 – Infraestructura** | Repo, Makefile (`nspire`/`host`/`test`), capa de plataforma mínima, "hola mundo" con `lcd_blit` en Firebird y en la CX II, **micro-benchmark del intérprete** (MIPS), prueba de `malloc` de 8 MB, temporizador fiable por sondeo | Binario funcionando en hardware real. Cifra de MIPS medida y decisión go/no-go del objetivo de la CE |
| **F1 – CPU Z80** | Núcleo Z80 en el host | ZEXDOC pasa |
| **F2 – TI-84+SE en host** | Memoria, puertos, LCD, timers, teclado y flash | TI-OS 2.55MP arranca en el host y se ve en PPM |
| **F3 – 84+ en la Nspire (MVP 1)** | Plataforma Nspire, pacing, menú, savestates y escritura de flash | Uso normal del TI-OS en la CX II al 100 % de velocidad |
| **F4 – Ficheros y variantes Z80** | Inyección HLE, `.8xk`, grises, 83+/83+SE/84+ | Doors CS y un juego en escala de grises funcionando |
| **F5 – CPU eZ80** | Núcleo eZ80 en el host y comparación con CEmu | 50 M de instrucciones del arranque sin divergencias |
| **F6 – CE en host** | Bus, flash, LCD, teclado, GPT, RTC, SHA y control | El OS de la CE arranca en el host |
| **F7 – CE en la Nspire (MVP 2)** | Plataforma, ruta LCD rápida y optimización del núcleo | OS usable y ≥ 60 % de velocidad |
| **F8 – CE: ficheros y pulido** | Inyección de variables CE, apps, Cesium, configuración y documentación | Programas C del toolchain funcionando |
| **F9 – Opcionales** | 84+CSE, JIT eZ80, exportación de variables, linkport virtual | Según interés |

---

## 11. Decisiones abiertas

1. Nombre definitivo del proyecto (provisional: N84).
2. ¿Prioridad Z80 → CE (recomendado, menos riesgo) o directamente la CE?
3. Idioma del código y de los comentarios (recomendado: inglés en el código y español en los documentos).
4. Tecla de acceso al menú del emulador.
5. ¿Escribir los núcleos de CPU desde cero (recomendado: control total del rendimiento) o adaptar los de z80e o CEmu?
