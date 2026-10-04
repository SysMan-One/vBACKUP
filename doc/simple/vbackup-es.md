# VBACKUP — muy fácil

VBACKUP guarda tus archivos en una «caja» grande.
Puedes poner la caja en una memoria USB.
Después puedes sacar todo de la caja otra vez.

```
   tu carpeta               caja (saveset)              memoria USB
  +-----------+            +--------------+            +-------+
  | /home/ivan|  ------->  |  ivan.bck    |  ------->  | /mnt/ |
  |  cartas   |  vbackup   | [][][][][][] |            |  usb  |
  |  fotos    |            +--------------+            +-------+
  +-----------+
```

---

## Las tres órdenes principales

Meter los archivos en una caja:

```
vbackup /home/ivan /mnt/usb/ivan.bck
```

Ver qué hay en la caja:

```
vbackup /mnt/usb/ivan.bck /LIST
```

Sacar todo otra vez (a una carpeta nueva):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

Esto es todo lo que necesitas. Abajo, paso a paso.

---

## Antes de empezar

1. Abre la ventana para órdenes. Se llama «terminal».
2. Escribe la orden tal como está aquí.
3. Pulsa la tecla **Enter**.

Reglas importantes:

- El nombre de una caja **siempre** termina en `.bck`. Por ejemplo: `ivan.bck`.
  Si olvidas `.bck`, VBACKUP no hace una caja. Solo copia la carpeta.
- Los espacios entre las partes de la orden son necesarios. No los quites.
- Las mayúsculas y las minúsculas importan: `/home/ivan` y `/Home/Ivan` son distintos.
- Si no aparece nada como respuesta, está bien. Quiere decir que funcionó.

En los ejemplos:

- `/home/ivan` es tu carpeta con archivos.
- `/mnt/usb` es la memoria USB.
- `/mnt/disk2` es un segundo disco.

Pon tus propios nombres en su lugar.

---

## 1. Guardar mis archivos

**Para qué:** para tener una copia si el ordenador se rompe.

**Qué escribir:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG /VERIFY
```

`/LOG` muestra cada archivo. `/VERIFY` revisa la caja enseguida.
Puedes no ponerlos; entonces VBACKUP trabaja en silencio.

**Qué verás:**

```
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-CREATED, /mnt/usb/ivan.bck created
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/letters saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/letters/anna.txt saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos/cat.jpg saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, ivan/photos/dog.jpg saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-SAVESUMM, 6 files, 50010 bytes saved in 4 blocks and 1 volume
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-VERIFYING, verifying /mnt/usb/ivan.bck
...
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-CMPSUMM, 6 files compared, 0 differences
```

**Qué quiere decir:**

- El principio de cada línea es la fecha, la hora y un número. Puedes no mirarlo.
- `saved` — el archivo está en la caja.
- `6 files ... saved` — en total se guardaron 6 cosas.
- `0 differences` — la caja está revisada y todo coincide. ¡Muy bien!

**¿Memoria USB pequeña o vieja (FAT32)?** Corta la caja en trozos de 4 GB:

```
vbackup /home/ivan /mnt/usb/ivan.bck /VOLUME_SIZE=4G
```

Los trozos se llaman `ivan.bck`, `ivan.bck.002`, `ivan.bck.003`…
Guárdalos siempre juntos, en una sola carpeta.

---

## 2. Ver qué hay dentro

**Para qué:** para saber qué hay en la caja sin sacar nada.

**Qué escribir:**

```
vbackup /mnt/usb/ivan.bck /LIST
```

**Qué verás (el final de la lista):**

```
ivan/
ivan/letters/
ivan/letters/anna.txt                                    10   4-OCT-2026 12:59
ivan/photos/
ivan/photos/cat.jpg                                   20000   4-OCT-2026 12:59
ivan/photos/dog.jpg                                   30000   4-OCT-2026 12:59

Total of 6 files, 50010 bytes
End of save set
```

**Qué quiere decir:**

- Cada línea es un archivo. Si termina en `/`, es una carpeta.
- El número es el tamaño del archivo. Después, cuándo se cambió por última vez.
- Arriba hay también una «cabecera»: cuándo y quién hizo la caja.

---

## 3. Recuperar mis archivos

**Para qué:** se perdieron o se estropearon archivos — los sacamos de la caja.

**Qué escribir:**

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /LOG
```

**Qué verás:**

```
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters/anna.txt restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/cat.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/dog.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTSUMM, 6 files, 50010 bytes restored
```

**Qué quiere decir:**

- `restored` — el archivo ha vuelto.
- Los archivos están en una carpeta nueva, `/home/ivan/restored/ivan/…`.
  Míralos allí con calma. No se tocó nada viejo.

Consejo: saca siempre los archivos a una carpeta **nueva y vacía**. Es más seguro.

---

## 4. Recuperar un solo archivo

**Para qué:** necesitas solo un archivo, no todos.

Primero mira el nombre exacto del archivo con la orden de la parte 2.
Por ejemplo: `ivan/letters/anna.txt`.

**Qué escribir:**

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt /home/ivan/anna.txt
```

**Qué verás:** nada. Está bien. El archivo ya está en `/home/ivan/anna.txt`.

También puedes ver el archivo en la pantalla, sin guardarlo:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
Dear Anna
```

Si el nombre está mal, verás:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, ivan/letters/nope.txt is not in the saveset
```

Quiere decir: ese archivo no está en la caja. Revisa el nombre en la lista (parte 2).

---

## 5. Guardar solo lo nuevo (cada día)

**Para qué:** para no copiar todo otra vez cada día, solo lo que cambió.

**Paso 1. Una vez — una caja grande con todo:**

```
vbackup /home/ivan /mnt/usb/full.bck /RECORD
```

```
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, 3 files recorded in the journal /var/lib/vbackup/vbackup.jnl
```

`/RECORD` quiere decir: recuerda lo que está guardado. VBACKUP tiene un cuaderno para esto: el «diario».

**Paso 2. Cada día — una caja pequeña solo con lo nuevo:**

```
vbackup /home/ivan /mnt/usb/mon.bck /SINCE=BACKUP /RECORD
```

```
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-RECORDED, 1 file recorded in the journal /var/lib/vbackup/vbackup.jnl
```

El diario es un cuaderno donde VBACKUP recuerda lo que ya guardó.
Para root está en `/var/lib/vbackup/vbackup.jnl`; para cualquier otro
usuario, en `~/.vbackup/vbackup.jnl` (en tu carpeta personal). No lo toques.

Al día siguiente usa otro nombre: `tue.bck`, luego `wed.bck`, y así.

**Recuperar todo de la cadena de cajas:** escríbelas con comas,
**de la más vieja a la más nueva**, y añade `/INCREMENTAL`:

```
vbackup /mnt/usb/full.bck,/mnt/usb/mon.bck,/mnt/usb/tue.bck /home/ivan/restored /INCREMENTAL /LOG
```

**Qué verás (el final):**

```
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, /home/ivan/restored/ivan/photos/dog.jpg deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, 13 files, 50021 bytes restored
```

**Qué quiere decir:**

- Los archivos vuelven como estaban el día de la última caja.
- `deleted` — borraste este archivo el martes, por eso no está. Así debe ser.

Recuerda: sin espacios después de las comas.

---

## 6. Copiar una carpeta a otro disco

**Para qué:** solo para hacer una copia de una carpeta, sin caja.

**Qué escribir** (el nombre **sin** `.bck`):

```
vbackup /home/ivan /mnt/disk2 /VERIFY
```

**Qué verás:** nada. Está bien. La carpeta ahora también está aquí: `/mnt/disk2/ivan`.

Con `/LOG`, VBACKUP muestra cada archivo:

```
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, /mnt/disk2/ivan/photos/cat.jpg copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, 6 files, 20021 bytes copied
```

`copied` — el archivo está copiado.

---

## 7. ¿No hay VBACKUP? Usa vbkx

**Para qué:** estás en otro ordenador y allí no está VBACKUP.
Pero existe `vbkx` — un solo programa pequeño. Llévalo en la misma memoria USB.

**Ver qué hay en la caja:**

```
vbkx l /mnt/usb/ivan.bck
```

```
2026-10-04 12:59:23         4096 d0755 ivan
2026-10-04 12:59:23         4096 d0755 ivan/letters
2026-10-04 12:59:23           10 -0644 ivan/letters/anna.txt
2026-10-04 12:59:23         4096 d0755 ivan/photos
2026-10-04 12:59:23        20000 -0644 ivan/photos/cat.jpg
2026-10-04 12:59:23        30000 -0644 ivan/photos/dog.jpg
```

La letra `d` es una carpeta, `-` es un archivo normal.

**Sacar todo a una carpeta:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored
```

No aparece nada — funcionó.

**Sacar solo una carpeta:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored ivan/photos
```

**Ver un archivo en la pantalla:**

```
vbkx p /mnt/usb/ivan.bck ivan/letters/anna.txt
```

```
Dear Anna
```

**Comprobar que la caja está entera:**

```
vbkx t /mnt/usb/ivan.bck
```

```
/mnt/usb/ivan.bck: all files read, all checksums match
```

Quiere decir: la caja está entera.

Si un archivo ya existe, `vbkx` no lo toca y dice:

```
vbkx: ivan/letters/anna.txt exists, not extracted (-f to overwrite)
```

Para reemplazarlo, añade `-f`.

---

## 8. Si algo salió mal

Un mensaje se ve así: `%VBACKUP-E-NOMBRE, texto`.
La letra después de `VBACKUP-` te dice lo grave que es:

- `I` — solo informa. Todo va bien.
- `W` — un aviso. Mira con atención.
- `E` o `F` — un error. Algo no se hizo.

### FILEEXISTS

**Qué ves:**

```
%VBACKUP-W-FILEEXISTS, /home/ivan/restored/ivan/letters/anna.txt already exists, not restored
```

**Qué pasó:** ese archivo ya está ahí. VBACKUP lo cuida y no lo toca.

**Qué hacer:** saca los archivos a una carpeta nueva y vacía:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored2
```

o reemplaza los archivos viejos (¡con cuidado!):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /REPLACE
```

### OPENOUT … errno=17 (File exists)

**Qué ves:**

```
%VBACKUP-E-OPENOUT, error creating /mnt/usb/ivan.bck as output, errno=17 (File exists)
```

**Qué pasó:** ya hay una caja con ese nombre.

**Qué hacer:** pon otro nombre:

```
vbackup /home/ivan /mnt/usb/ivan2.bck
```

o escribe encima de la caja vieja (¡la vieja desaparecerá!):

```
vbackup /home/ivan /mnt/usb/ivan.bck /REPLACE
```

### NOTSAVESET

**Qué ves:**

```
%VBACKUP-E-NOTSAVESET, /mnt/usb/fake.bck is not a saveset
```

**Qué pasó:** este archivo no es una caja de VBACKUP.

**Qué hacer:** revisa el nombre. Mira qué hay en la memoria USB:

```
ls /mnt/usb
```

### NOPARAM

**Qué ves:**

```
%VBACKUP-E-NOPARAM, missing parameter: input specification - it does not exist
```

o

```
%VBACKUP-E-NOPARAM, missing parameter: output specification
```

**Qué pasó:** en el primer caso, ese archivo no existe (una errata en el nombre).
En el segundo, pediste `/LIST`, pero el archivo no es una caja; VBACKUP pensó
que querías guardar algo y no dijiste dónde.

**Qué hacer:** revisa el nombre con `ls /mnt/usb`.

### IVOP

**Qué ves:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist
```

**Qué pasó:** lo primero que escribiste no existe. Seguramente es una errata.

**Qué hacer:** revisa el nombre:

```
ls /home/ivan
```

### BLKFIXED

**Qué ves:**

```
%VBACKUP-I-BLKFIXED, block 3 of volume 1 was bad and has been rebuilt from its group
```

**Qué pasó:** un trocito de la caja estaba roto, pero VBACKUP lo arregló solo.
**Todos los archivos están bien.**

**Qué hacer:** quizá la memoria USB empieza a fallar.
Pronto haz una caja nueva en otra memoria USB.

### BLKLOST y FILDAMAGED

**Qué ves:**

```
%VBACKUP-E-BLKLOST, block 3 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, block 4 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/photo1.jpg is incomplete: its data was lost in bad blocks
```

**Qué pasó:** una parte de la caja está rota y no se pudo arreglar.
Los archivos nombrados en `FILDAMAGED` volvieron incompletos. **Todos los demás están bien.**

**Qué hacer:** saca esos archivos de otra caja, si la tienes.
La próxima vez haz dos cajas en dos memorias USB distintas.

### FILLOST

**Qué ves:**

```
%VBACKUP-E-FILLOST, /home/ivan/restored/ivan/photo10.jpg was not restored: its records were lost in bad blocks
```

**Qué pasó:** este archivo se perdió con el trozo roto. No está en absoluto.

**Qué hacer:** sácalo de otra caja más vieja:

```
vbackup /mnt/usb/old.bck /LIST
```

### UNNAMED

**Qué ves:**

```
%VBACKUP-W-UNNAMED, /mnt/usb/ivan.bck: blocks were lost and it has no catalog - files missing from the restore cannot all be named
```

**Qué pasó:** la caja está rota y su «índice» también se perdió.
Pueden faltar archivos, pero VBACKUP no puede nombrarlos todos.

**Qué hacer:** compara lo que volvió con la lista de otra caja:

```
vbackup /mnt/usb/old.bck /LIST
```

### MISSVOL

**Qué ves:**

```
%VBACKUP-E-MISSVOL, volume 2 of /mnt/usb/ivan.bck is missing
```

**Qué pasó:** la caja se cortó en trozos y falta uno
(por ejemplo `ivan.bck.002`).

**Qué hacer:** pon todos los trozos en una carpeta, sin cambiar sus nombres, y prueba otra vez:

```
ls /mnt/usb
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### NOTRAILER

**Qué ves:**

```
%VBACKUP-W-NOTRAILER, /mnt/usb/ivan.bck has no trailer: the save did not complete, or its last volume is missing
```

**Qué pasó:** el guardado no terminó (se fue la luz, el disco se llenó)
o falta el último trozo.

**Qué hacer:** los archivos hasta el corte se pueden sacar como siempre.
Después haz la caja otra vez.

### NOTINCR

**Qué ves:**

```
%VBACKUP-E-NOTINCR, /mnt/usb/ivan.bck: it has no catalog, nothing restored with /INCREMENTAL
```

**Qué pasó:** `/INCREMENTAL` necesita una caja entera con «índice». Esta no lo tiene.

**Qué hacer:** sácala sin `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

---

## 9. Ayuda, no entiendo nada

No pasa nada. Pide ayuda al mismo programa.

Una pista corta:

```
vbackup
```

La ayuda completa, en inglés (página siguiente — **Enter**, salir — **Q**):

```
vbackup /HELP
```

Qué hacer con los errores:

```
vbackup /HELP TROUBLESHOOTING
```

El manual, en inglés (salir — la tecla **q**):

```
man vbackup
man vbkx
```

Y sobre todo: pide ayuda a quien te preparó el ordenador. Enséñale el mensaje.

---

## Pequeño diccionario

- **Carpeta** — un sitio donde viven los archivos. Como un cajón de un armario.
- **Archivo** — una carta, una foto, una canción.
- **Orden** — una línea que escribes en el terminal y envías con la tecla Enter.
- **Terminal** — la ventana donde escribes las órdenes.
- **Saveset (caja)** — un archivo grande que guarda todos tus archivos. Su nombre termina en `.bck`.
- **Volumen (trozo)** — una parte de una caja cortada: `ivan.bck.002`, `ivan.bck.003`.
- **Memoria USB** — un disco pequeño que se enchufa al ordenador.
- **Diario** — el cuaderno de VBACKUP donde apunta lo que ya está guardado.
- **Bloque** — un trocito de la caja. Si uno se rompe, VBACKUP lo arregla solo.
- **Índice (catálogo)** — la lista de todos los archivos al final de la caja.
