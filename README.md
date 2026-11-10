# N84

Emulador de TI-84 Plus (y, más adelante, TI-84 Plus CE) para la TI-Nspire CX II con Ndless.
Licencia GPLv3. Especificación en [docs/SPEC.md](docs/SPEC.md); viabilidad en [docs/ANALISIS.md](docs/ANALISIS.md); requisitos en [docs/REQUISITOS.md](docs/REQUISITOS.md).

## Estado

| Fase | Estado |
|---|---|
| F0 Infraestructura | Hecha en el host. **Falta validarla en la Firebird y en la calculadora real** con `n84bench.tns` |
| F1 Núcleo Z80 | ✅ Pasa ZEXDOC y ZEXALL (67/67) |
| F2 TI-84+ en el host | ✅ Arranca el TI-OS 2.55MP real: cálculos y gráficas (`make rom-smoke`) |
| F3 TI-84+ en la Nspire | ✅ Funciona en la Firebird (cálculos básicos). Falta probarlo en la calculadora real |
| F4 Ficheros y variantes Z80 | Pendiente |
| F5 Núcleo eZ80 | ✅ Pasa ZEXDOC en modo Z80 (salvo los opcodes que en un eZ80 real provocan trap) y 30 tests propios de ADL, sufijos, MBASE y llamadas mixtas |
| F7 Optimización eZ80 | ✅ Ruta rápida especializada por modo, con caché de la página de código y despacho por tabla de etiquetas (`ez80_run.inc`). Benchmark en la **CX II real: 86 % de una CE** de media (75–101 % según cómo caigan los datos en la caché), antes 55 %. La Firebird da cifras ~1,8 veces optimistas; para comparar cambios sin la calculadora está `tools/arm926sim` (modelo de ciclos del ARM926). Un test diferencial (`ez80_diff`) compara la ruta rápida con el decodificador genérico instrucción a instrucción |
| F6 TI-84 Plus CE | ✅ En el PC: arranca el OS 5.3.0, calcula y grafica; guardar/cargar estado. Periféricos adaptados de CEmu (`src/machine/ce/README.md`). En la Nspire: `n84.tns` abre ROMs de 4 MB, **pendiente de probar** |
| F8, F9 | Pendientes |

Modelos soportados: TI-83 Plus SE, TI-84 Plus, TI-84 Plus SE (Z80) y TI-84 Plus CE con flash paralela
(revisiones anteriores a la M, boot < 5.3.6). La TI-83 Plus "básica" (512 KB) y las CE con flash serie se añadirán después.

## Compilar

```sh
make host            # herramientas del PC: tests, benchmark, n84-host
make test            # ZEXDOC + test de la máquina TI-84+ con ROM sintética
make rom-smoke       # con tus ROMs en roms/: arranque, cálculo, gráfica y prueba del lanzador (n84-ui)
make test-full       # además ZEXALL (~40 s)
export PATH=$HOME/Ndless/ndless-sdk/bin:$PATH
make nspire          # build/nspire/n84.tns y build/nspire/n84bench.tns
```

## Probar en la calculadora (o en la Firebird)

1. **Benchmark de la fase 0:** copia `build/nspire/n84bench.tns` y ejecútalo. Muestra y guarda en
   `n84bench.txt.tns` lo siguiente: tipo de hardware y de LCD, si el timer funciona, si se pueden reservar
   bloques de 4, 8 y 16 MB, los FPS del `lcd_blit`, la velocidad del núcleo Z80 en MHz equivalentes
   y la del eZ80 en MIPS.
   **Pásame ese fichero** (o una foto de la pantalla).
2. **Emulador:** copia `build/nspire/n84.tns` y tus volcados de ROM (`NOMBRE.rom.tns`) **en la misma carpeta**.
   - Al abrir `n84.tns` aparece el **lanzador**: lista de ROMs con su modelo (`*` = tiene estado guardado).
     `enter` ejecuta, `menu` (o `?!▸`) abre las opciones (información, borrar estado, ajustes), `esc` sale.
   - También se puede abrir una ROM directamente desde Mis documentos.
   - Dentro de la emulación, `?!▸` (o `ctrl + esc`) abre el menú de N84: continuar, guardar/cargar estado,
     resetear, turbo, indicador de velocidad, volver al lanzador o salir. En la Firebird usa `?!▸` del panel
     Keypad, porque Windows se queda `ctrl + esc`.
   - El estado se guarda en `NOMBRE.sav.tns` al salir. Los cambios en la memoria Archive se escriben
     **dentro del propio fichero ROM**, así que guarda una copia del volcado original.
   - Ajustes globales en `n84.cfg.tns`.
   - **Transferir programas** (`.8xp`, `.8xv`, `.8xg`…, con `.tns` al final, p. ej. `JUEGO.8xp.tns`):
     desde el lanzador (opciones de la ROM → *Transfer files...*) o desde el menú del juego (*Send file...*),
     eligiendo el fichero en el explorador. Se respetan los marcados como archivados. Para crear las
     variables la calculadora emulada pasa un momento por la pantalla MODE y vuelve a la principal
     (se pierde lo que estuviera a medio escribir). Las aplicaciones Flash (`.8xk`/`.8ek`) aún no.

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
build/host/n84ce-host ROMCE.rom 9 -t -o pantalla.ppm 5:CLEAR 5.5:2 7:ENTER   # TI-84 Plus CE
```


```sh
build/host/n84-host ROM.rom 10 -t -o pantalla.ppm 5.0:ENTER 6.0:shot=paso.ppm
```

Ejecuta 10 segundos emulados, pulsa ENTER en t=5 s, hace una captura en t=6 s y otra al final.
