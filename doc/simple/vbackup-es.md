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

Y más: hacer la caja más pequeña, sección 7; guardar un disco entero, secciones 8 y 9.

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

## 7. Hacer la caja más pequeña

**Para qué:** para que la caja ocupe menos sitio en la memoria USB.

**Qué escribir:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /DATA_FORMAT=COMPRESSED
```

**Qué verás:** nada. Eso es bueno. La caja está lista, y es más pequeña.

Compara la misma carpeta sin y con compresión:

```
sin compresión:  1048576 bytes
con compresión:   655360 bytes
```

**Qué significa:** las cartas y los documentos se encogen bien.
Las fotos, los vídeos y los archivos comprimidos (`.jpg`, `.mp4`, `.zip`) ya
están comprimidos: VBACKUP los guarda tal cual. No se rompe nada.

Sacar una caja pequeña es igual que siempre, no hay que añadir nada:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

**Cuidado:** un VBACKUP antiguo (antes de X01-04) no entiende una caja
pequeña. Dice que los archivos están dañados:

```
%VBACKUP-E-CRCERR, /home/ivan/restored/ivan/letters/letter1.txt: checksum mismatch, the data differ from what was saved
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/letters/letter1.txt is incomplete: its data was lost in bad blocks
```

Los archivos de la caja están bien. Solo instala el VBACKUP nuevo.

---

## 8. Guardar un disco o una partición entera, tal como está

**Para qué:** para hacer una copia exacta de todo el disco, trocito a trocito.
Así se copia el disco con el que arranca el ordenador, o un disco cifrado.

Esto solo lo puede hacer el usuario jefe del ordenador: **root**.

**Primero averigua el nombre del disco.** Esto es muy importante:

```
lsblk
```

Verás una lista de discos, por ejemplo `sdb`, y en él una partición `sdb1`.
El nombre completo de la partición es `/dev/sdb1`.

**Antes de guardar:** nadie puede escribir en ese disco.
Desconéctalo del sistema (`umount`) o conéctalo «solo para leer».
Si no, la copia sale rota y no te darás cuenta.

**Qué escribir:**

```
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

**Qué verás:**

```
%VBACKUP-I-PHYSSUMM, /dev/sdb1: 67108864 bytes, 1507328 of them data, the rest zeros
```

**Qué significa:** se guardó un disco de 64 MB. Tenía 1,5 MB de datos de
verdad. Los sitios vacíos (ceros) no ocupan sitio en la caja.

**Volver a ponerlo en un disco.** ¡Esto **borra todo** lo que hay en `/dev/sdc1`!
Comprueba el nombre con `lsblk` dos veces.

```
vbackup /mnt/usb/sdb1.bck /dev/sdc1 /PHYSICAL /REPLACE
```

VBACKUP pregunta:

```
Everything on /dev/sdc1 (134217728 bytes) is to be overwritten with the device saved in /mnt/usb/sdb1.bck.
Type YES to go on:
```

Escribe `YES` en mayúsculas y pulsa Enter. Cualquier otra respuesta es «no».

**Qué verás:**

```
%VBACKUP-I-PHYSLARGER, /dev/sdc1 holds 134217728 bytes, the device saved held 67108864: the rest stays as it is, the file system keeps its old size
%VBACKUP-I-PHYSUUID, /dev/sdc1 now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-PHYSSUMM, /dev/sdc1: 67108864 bytes, 1507328 of them data, the rest zeros
```

**Qué significa:**

- el disco nuevo es más grande: la copia queda al principio, el resto no se toca;
- la copia es gemela del disco viejo. **Nunca conectes** el viejo y el nuevo
  a la vez: se confunden;
- un disco más pequeño que el guardado no sirve: VBACKUP se niega.

**Ponerlo en un archivo imagen** (no borra nada):

```
vbackup /mnt/usb/sdb1.bck /home/ivan/sdb1.img /PHYSICAL
```

---

## 9. Guardar un sistema de archivos entero y crearlo de nuevo en otro disco

**En qué se diferencia de la sección 8:** la sección 8 copia cada trocito del
disco; aquí VBACKUP copia todos los **archivos** del disco y recuerda qué disco era.
El disco nuevo puede tener otro tamaño.

Esto también lo hace solo **root**.

**Qué escribir** (`/mnt/photos` es donde está conectado el disco):

```
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

**Qué verás:** nada. Eso es bueno.

**Crear el disco de nuevo en `/dev/sdc1`.** ¡Esto **borra todo** en `/dev/sdc1`!

```
vbackup /mnt/usb/photos.bck /dev/sdc1 /IMAGE /REPLACE
```

**Qué verás:**

```
%VBACKUP-I-PHYSUUID, /dev/sdc1 now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-IMGSUMM, /dev/sdc1: a ext4 file system made, 11 files, 760000 bytes restored
```

**Qué significa:** en `/dev/sdc1` se hizo un disco nuevo del mismo tipo (ext4)
con el mismo nombre, y se pusieron en él todos los archivos.

Recuerda:

- hace falta el programa que crea discos de ese tipo (`mkfs.ext4`,
  `mkfs.vfat` …). Lo instala quien te preparó el ordenador;
- así **no** se puede mover el disco con el que arranca el ordenador.
  Para eso guarda el disco entero (`/dev/sdb`, no `/dev/sdb1`) como en la sección 8;
- no conectes el disco viejo y el nuevo a la vez: son gemelos.

---

## 10. ¿No hay VBACKUP? Usa vbkx

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

## 11. Si algo salió mal

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

### PHYSMOUNTED

**Qué ves:**

```
%VBACKUP-E-PHYSMOUNTED, /dev/sdb1 is mounted read-write on /mnt/photos: unmount it, mount it read-only, or save a snapshot
```

**Qué pasó:** el disco está conectado y se puede escribir en él. La copia saldría rota.

**Qué hacer:** desconecta el disco y vuelve a intentarlo:

```
umount /mnt/photos
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

Al devolver una caja, el mismo mensaje significa: VBACKUP no escribe nada en un
disco conectado. Desconéctalo.

### PHYSHELD

**Qué ves:**

```
%VBACKUP-E-PHYSHELD, /dev/sdb2 is in use (swap): free it first, or save what uses it
```

**Qué pasó:** el propio sistema usa este disco (swap, LVM, RAID, cifrado).

**Qué hacer:** pide ayuda a quien te preparó el ordenador. No toques este disco tú solo.

### PHYSREPLACE

**Qué ves:**

```
%VBACKUP-E-PHYSREPLACE, /dev/sdc1 is a device: everything on it is overwritten - give /REPLACE to do so
```

**Qué pasó:** VBACKUP protege el disco: sin `/REPLACE` no lo borra.

**Qué hacer:** comprueba el nombre del disco (`lsblk`). Si seguro que es ese, añade `/REPLACE`.

### PHYSSMALL

**Qué ves:**

```
%VBACKUP-E-PHYSSMALL, /dev/sdc1 holds 33554432 bytes, the device saved held 67108864: nothing written
```

**Qué pasó:** el disco nuevo es más pequeño que el guardado. No cabe todo.

**Qué hacer:** usa un disco más grande. O pon la caja en un archivo imagen (sección 8).

### PHYSABORT

**Qué ves:**

```
%VBACKUP-E-PHYSABORT, /dev/sdc1 not overwritten: the answer was not YES
```

**Qué pasó:** no contestaste `YES`. No se borró nada.

**Qué hacer:** si de verdad lo quieres, repite y escribe `YES` en mayúsculas.

### IMGNOTVOL

**Qué ves:**

```
%VBACKUP-E-IMGNOTVOL, /home/ivan is neither the mount point of a file system nor a device: /IMAGE saves a whole volume
```

**Qué pasó:** `/IMAGE` guarda un disco entero, y tú diste una carpeta normal.

**Qué hacer:** da el sitio donde está conectado el disco (por ejemplo `/mnt/photos`),
o guarda la carpeta de la forma normal (sección 1).

### IMGNOTMNT

**Qué ves:**

```
%VBACKUP-E-IMGNOTMNT, /dev/sdb1 is not mounted: mount it (read-only is enough) and give the mount point or the device
```

**Qué pasó:** el disco no está conectado: VBACKUP no puede leer sus archivos.

**Qué hacer:** conéctalo solo para leer y vuelve a intentarlo:

```
mount -o ro /dev/sdb1 /mnt/photos
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

### IMGUNSUPP

**Qué ves:**

```
%VBACKUP-E-IMGUNSUPP, /mnt/usb/old.bck: VBACKUP does not make a file system of type minix - use /PHYSICAL for it
```

**Qué pasó:** VBACKUP no sabe crear un disco de ese tipo.

**Qué hacer:** guarda ese disco como en la sección 8 (`/PHYSICAL`).
O saca de la caja solo los archivos (sección 3).

### IMGMKFS

**Qué ves** (por ejemplo):

```
%VBACKUP-E-IMGMKFS, mkfs.xfs -f -q -L PHOTOS /dev/sdc1 failed: the program is not installed
```

**Qué pasó:** no se pudo crear el disco nuevo. Casi siempre falta el programa.

**Qué hacer:** pide a quien te preparó el ordenador que lo instale (aquí, `mkfs.xfs`).

### IMGSMALL

**Qué ves:**

```
%VBACKUP-E-IMGSMALL, /dev/sdc1 holds 8388608 bytes, the files need about 17596518: nothing written
```

**Qué pasó:** los archivos no caben en este disco.

**Qué hacer:** usa un disco más grande.

---

## 12. Ayuda, no entiendo nada

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
- **Partición** — una parte de un disco. Un disco se puede dividir en varias particiones: `sdb1`, `sdb2`.
- **Sistema de archivos** — el orden en que los archivos están en un disco. Tiene un tipo: ext4, vfat …
- **Punto de montaje** — la carpeta por la que ves un disco conectado. Por ejemplo `/mnt/photos`.
- **Imagen** — un solo archivo que guarda un disco entero, trocito a trocito.
- **UUID** — el número largo de un disco, como un pasaporte. Una copia tiene el mismo que el original.
