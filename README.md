# N84

Emulador de TI-84 Plus (y, más adelante, TI-84 Plus CE) para la TI-Nspire CX II con Ndless.
Licencia GPLv3. Especificación en [docs/SPEC.md](docs/SPEC.md); viabilidad en [docs/ANALISIS.md](docs/ANALISIS.md); requisitos en [docs/REQUISITOS.md](docs/REQUISITOS.md).

## Estado

| Fase | Estado |
|---|---|
| F0 Infraestructura | Hecha en el host. **Falta validarla en la Firebird y en la calculadora real** con `n84bench.tns` |
| F1 Núcleo Z80 | ✅ Pasa ZEXDOC y ZEXALL (67/67) |
| F2 TI-84+ en el host | ✅ Hardware implementado y probado con una ROM sintética. **Falta arrancar el TI-OS real** (no hay ROM todavía) |
| F3 TI-84+ en la Nspire | Código hecho (`n84.tns`), **sin probar en hardware** |
| F4–F9 | Pendientes (siguiente: núcleo eZ80) |

Modelos Z80 soportados: TI-83 Plus SE, TI-84 Plus y TI-84 Plus SE. La TI-83 Plus "básica" (512 KB) se añadirá después.

## Compilar

```sh
make host            # herramientas del PC: tests, benchmark, n84-host
make test            # ZEXDOC + test de la máquina TI-84+ con ROM sintética
make test-full       # además ZEXALL (~40 s)
export PATH=$HOME/Ndless/ndless-sdk/bin:$PATH
make nspire          # build/nspire/n84.tns y build/nspire/n84bench.tns
```

## Probar en la calculadora (o en la Firebird)

1. **Benchmark de la fase 0:** copia `build/nspire/n84bench.tns` y ejecútalo. Muestra y guarda en
   `n84bench.txt.tns` lo siguiente: tipo de hardware y de LCD, si el timer funciona, si se pueden reservar
   bloques de 4, 8 y 16 MB, los FPS del `lcd_blit` y la velocidad del núcleo Z80 en MHz equivalentes.
   **Pásame ese fichero** (o una foto de la pantalla).
2. **Emulador:** copia `build/nspire/n84.tns` y ejecútalo una vez para registrar la extensión `.rom`.
   Copia tu volcado de ROM como `NOMBRE.rom.tns` y ábrelo desde Mis Documentos.
   - `ctrl + esc`: menú (continuar / turbo / guardar y salir).
   - El estado se guarda en `NOMBRE.sav.tns`. Los cambios en la memoria Archive se escriben
     **dentro del propio fichero ROM**, así que guarda una copia del volcado original.

### Teclado (por defecto)

| Nspire | TI-84 | | Nspire | TI-84 |
|---|---|---|---|---|
| ctrl | 2ND | | scratchpad | Y= |
| shift | ALPHA | | tab | WINDOW |
| esc | CLEAR | | doc | ZOOM |
| del | DEL | | menu | TRACE |
| home/on | ON | | cat | GRAPH |
| var | VARS | | templates | MODE |
| trig | STAT | | = | STO→ |
| ^ / x² | ^ / x² | | 10^x / e^x | LOG / LN |
| EE | x⁻¹ | | flag | PRGM |
| X | X,T,θ,n | | espacio | 0 |

Las letras A–Z van a la tecla de la 84 cuya función ALPHA es esa letra (A=MATH, B=APPS,
C=PRGM, …), así que `shift` + letra escribe la letra.

## Probar una ROM en el PC

```sh
build/host/n84-host ROM.rom 10 -t -o pantalla.ppm 5.0:ENTER 6.0:shot=paso.ppm
```

Ejecuta 10 segundos emulados, pulsa ENTER en t=5 s, hace una captura en t=6 s y otra al final.
