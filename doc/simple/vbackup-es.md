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

Y más: guardar la caja en otro ordenador, sección 7; hacerla más pequeña, sección 8;
cerrarla con contraseña, sección 9; guardar un disco entero, secciones 10 y 11.

---

## Antes de empezar

1. Abre la ventana para órdenes. Se llama «terminal».
2. Escribe la orden tal como está aquí.
3. Pulsa la tecla **Enter**.

Reglas importantes:

- El nombre de una caja **siempre** termina en `.bck` o `.sav`. Por ejemplo: `ivan.bck`.
  Si lo olvidas, VBACKUP no hace una caja. Solo copia la carpeta.
  (¿Otro nombre? Entonces añade `/SAVE_SET`.)
- Una palabra como `/LOG` o `/LIST` es un «calificador». **Siempre** empieza por `/`.
  Si escribes `.log` en vez de `/LOG`, VBACKUP dice `MAXPARM` y no hace nada.
- Un calificador pegado al nombre también funciona: `box.sav/sav/log`. Entonces VBACKUP dice `GLUED`.
- Los espacios entre las partes de la orden son necesarios. No los quites.
- Las mayúsculas y las minúsculas importan: `/home/ivan` y `/Home/Ivan` son distintos.
- Al final VBACKUP escribe `completed`. Está bien. Quiere decir que funcionó.

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
Puedes no ponerlos; entonces VBACKUP muestra solo el principio, el total y el final.

**Qué verás:**

```
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/ivan.bck - started
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-CREATED, Volume: /mnt/usb/ivan.bck - created
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/letters - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/letters/anna.txt - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos/cat.jpg - saved
04-10-2026 12:59:32.284 2278116 %VBACKUP-I-SAVED, File: ivan/photos/dog.jpg - saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-SAVESUMM, Files: 6, Bytes: 50010, Blocks: 4, Volumes: 1 - saved
04-10-2026 12:59:32.288 2278116 %VBACKUP-I-VERIFYING, Saveset: /mnt/usb/ivan.bck - verifying
...
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-CMPSUMM, Files: 6, Differences: 0 - compared
04-10-2026 12:59:32.292 2278116 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

**Qué quiere decir:**

- El principio de cada línea es la fecha, la hora y un número. Puedes no mirarlo.
- `started` y `completed` — el trabajo empezó y terminó. `completed` quiere decir que todo salió bien.
- `saved` — el archivo está en la caja.
- `Files: 6 ... saved` — en total se guardaron 6 cosas.
- `Differences: 0` — la caja está revisada y todo coincide. ¡Muy bien!

**¿Memoria USB pequeña o vieja (FAT32)?** Corta la caja en trozos de 4 GB:

```
vbackup /home/ivan /mnt/usb/ivan.bck /VOLUME_SIZE=4G
```

Los trozos se llaman `ivan.bck`, `ivan.bck.002`, `ivan.bck.003`…
Guárdalos siempre juntos, en una sola carpeta.

**Consejo: una caja que aguanta más daños.** Si la memoria USB o el disco
son viejos, añade `/PARITY=2`: la caja crece un poco (un 20%), pero incluso
dos trozos rotos juntos se reparan solos.

```
vbackup /home/ivan /mnt/usb/ivan.bck /PARITY=2
```

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
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/ivan.bck, Output: /home/ivan/restored - started
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/letters - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/letters/anna.txt - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos/cat.jpg - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, File: /home/ivan/restored/ivan/photos/dog.jpg - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTSUMM, Files: 6, Bytes: 50010 - restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.00 - completed
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

**Qué verás:**

```
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck, Output: /home/ivan/anna.txt - started
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

Está bien. El archivo ya está en `/home/ivan/anna.txt`.

También puedes ver el archivo en la pantalla, sin guardarlo:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck - started
Dear Anna
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

Si el nombre está mal, verás:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, File: ivan/letters/nope.txt - is not in the saveset
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
04-10-2026 13:00:01.548 2284063 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/full.bck - started
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-SAVESUMM, Files: 4, Bytes: 1842, Blocks: 4, Volumes: 1 - saved
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, Files: 3, Journal: /var/lib/vbackup/vbackup.jnl - recorded
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

`/RECORD` quiere decir: recuerda lo que está guardado. VBACKUP tiene un cuaderno para esto: el «diario».

**Paso 2. Cada día — una caja pequeña solo con lo nuevo:**

```
vbackup /home/ivan /mnt/usb/mon.bck /SINCE=BACKUP /RECORD
```

```
04-10-2026 13:00:03.285 2284360 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/mon.bck - started
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-SAVESUMM, Files: 2, Bytes: 120, Blocks: 4, Volumes: 1 - saved
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-INCRSUMM, Files: 2 - unchanged, listed as present, not saved
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-RECORDED, Files: 1, Journal: /var/lib/vbackup/vbackup.jnl - recorded
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
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
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, File: /home/ivan/restored/ivan/photos/dog.jpg - deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, Files: 13, Bytes: 50021 - restored
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.02 - completed
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

**Qué verás:**

```
04-10-2026 13:00:12.410 2285890 %VBACKUP-I-STARTED, Operation: copy, Input: /home/ivan, Output: /mnt/disk2 - started
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

Está bien. La carpeta ahora también está aquí: `/mnt/disk2/ivan`.

Con `/LOG`, VBACKUP muestra cada archivo:

```
...
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, File: /mnt/disk2/ivan/photos/cat.jpg - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

`copied` — el archivo está copiado.

---

## 7. Guardar la caja en otro ordenador

**Para qué:** si tu ordenador se rompe, se quema o te lo roban, la caja está a salvo, lejos.
VBACKUP puede poner la caja directamente en otro ordenador, por la red.

Escribe el nombre del otro ordenador, dos veces dos puntos `::` y el nombre de la caja allí.
En los ejemplos el otro ordenador se llama `backup-host`:

```
vbackup /home/ivan backup-host::/backup/ivan.bck /VERIFY
```

**Qué verás:**

```
05-10-2026 10:15:02.118 3104417 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: backup-host::/backup/ivan.bck - started
05-10-2026 10:15:02.364 812230 %VBACKUP-I-STARTED, Operation: copy of a saveset, Input: (standard input), Output: /backup/ivan.bck - started
05-10-2026 10:15:02.371 3104417 %VBACKUP-I-SAVESUMM, Files: 6, Bytes: 9218, Blocks: 4, Volumes: 1 - saved
05-10-2026 10:15:02.379 812230 %VBACKUP-I-XFRSUMM, Blocks: 4, Volumes: 1, Bad: 0 - copied
05-10-2026 10:15:02.379 812230 %VBACKUP-I-COMPLETED, Operation: copy of a saveset, Seconds: 0.01 - completed
05-10-2026 10:15:02.383 3104417 %VBACKUP-I-VERIFYING, Saveset: backup-host::/backup/ivan.bck - verifying
05-10-2026 10:15:02.536 3104417 %VBACKUP-I-CMPSUMM, Files: 6, Differences: 0 - compared
05-10-2026 10:15:02.536 3104417 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.42 - completed
```

**Qué quiere decir:** trabajan dos VBACKUP juntos, uno aquí y otro allí.
Los dos escriben líneas en tu pantalla. Los distingues por el número después de la hora:

- `3104417` — es **nuestro** VBACKUP. `save` y `saved` — mete tus archivos en la caja.
- `812230` — es el VBACKUP del **otro ordenador**. `copy of a saveset` y `copied` —
  recibe la caja, revisa cada bloque y la escribe allí.
  `(standard input)` quiere decir: la caja le llega por la red, no de un archivo.
- `Bad: 0` — no llegó ningún bloque roto. Bien.
- `/VERIFY` lee la caja otra vez desde el otro ordenador y la compara con tus archivos.
  `Differences: 0` — todo coincide.
- El último `completed` es el nuestro. Quiere decir que todo salió bien en los dos ordenadores.

**Recuperar tus archivos desde allí:**

```
vbackup backup-host::/backup/ivan.bck /home/ivan/restored
```

Verás lo mismo que en la sección 3, y también unas líneas del otro ordenador.

**Ver qué hay en la caja de allí:**

```
vbackup backup-host::/backup/ivan.bck /LIST
```

**¿Una caja grande en trozos?** `/VOLUME_SIZE` también funciona aquí:

```
vbackup /home/ivan backup-host::/backup/ivan.bck /VOLUME_SIZE=4G
```

Los trozos aparecen en el otro ordenador: `ivan.bck`, `ivan.bck.002`, `ivan.bck.003`…

**Qué hace falta:**

- VBACKUP X01-11 o más nuevo, instalado en **los dos** ordenadores.
- El otro ordenador te deja entrar con **claves ssh**, sin pedir contraseña.
  ¿No sabes cómo? No pasa nada: pide a tu administrador
  (quien te preparó el ordenador) que te configure las claves ssh.

**Copiar una caja tal cual**

Puedes copiar una caja a otro sitio bloque a bloque, sin deshacerla.
Escribe primero la caja y después la caja nueva:

```
vbackup /home/ivan/ivan.bck /mnt/usb/ivan.bck
```

Se copian todos sus trozos, y cada bloque se revisa por el camino.
Así se puede copiar incluso una caja cerrada (sección 9) — **no hace falta la contraseña**:
la copia sigue cerrada, igual que el original.

También funciona con otro ordenador:

```
vbackup /home/ivan/ivan.bck backup-host::/backup/ivan.bck
```

---

## 8. Hacer la caja más pequeña

**Para qué:** para que la caja ocupe menos sitio en la memoria USB.

**Qué escribir:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /DATA_FORMAT=COMPRESSED
```

**Qué verás:** el principio, el total y `completed`, como en la parte 1. La caja está lista, y es más pequeña.

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

**Aún más pequeña.** Añade `/LEVEL=` y un número del 1 al 9. Cuanto más
alto, más pequeña la caja y más tarda en hacerse. Sacar los archivos es
rápido con cualquier número.

```
vbackup /home/ivan /mnt/usb/ivan.bck /LEVEL=6
```

`/LEVEL=1` es el rápido (igual que `/DATA_FORMAT=COMPRESSED`); `/LEVEL=5`
es como zip; `/LEVEL=9` es como 7-Zip. Las cartas se reducen unas 2 veces
con 1, casi 4 con 5 y casi 5 con 9.

**¿Es seguro?** Sí. VBACKUP desempaqueta cada trozo justo después de
empaquetarlo y lo compara con tu archivo. Solo se guarda empaquetado el
trozo que vuelve igual.

**Muchos archivos pequeños.** Cuando VBACKUP empaqueta, junta los archivos
pequeños (cartas, notas, programas pequeños) en paquetes de hasta 256 KB y
empaqueta cada paquete de una vez. Los archivos parecidos comparten lo que
tienen en común, y la caja queda mucho más pequeña: una carpeta de 22 444
archivos pequeños con `/LEVEL=9` ocupaba 53 MB, y ahora ocupa 33 MB. Esto funciona solo:
no tienes que escribir nada.

**Lo que cuesta.** Si la memoria USB se rompe mucho y un trozo de la caja no
se puede reparar, se pierde más de un archivo: los archivos del paquete desde
el sitio roto en adelante, de unos pocos a un par de decenas de archivos
pequeños. Los archivos del paquete antes del sitio roto vuelven igual,
enteros y comprobados. VBACKUP te dice el nombre de cada uno que se pierde
(`FILLOST`). Un trozo roto se sigue reparando solo, como siempre. Si la caja
tiene que aguantar un disco muy roto, añade `/PARITY=2` (mira la sección 1),
o apaga los paquetes con `/NOSOLID`:

```
vbackup /home/ivan /mnt/usb/ivan.bck /LEVEL=6 /PARITY=2
vbackup /home/ivan /mnt/usb/ivan.bck /LEVEL=6 /NOSOLID
```

**Cuidado:** un VBACKUP antiguo (antes de X01-04) no entiende una caja
pequeña. Dice que los archivos están dañados:

```
%VBACKUP-E-CRCERR, /home/ivan/restored/ivan/letters/letter1.txt: checksum mismatch, the data differ from what was saved
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/letters/letter1.txt is incomplete: its data was lost in bad blocks
```

Los archivos de la caja están bien. Solo instala el VBACKUP nuevo.

**Cuidado también:** un VBACKUP anterior a X01-21 no abre en absoluto una
caja pequeña hecha por un VBACKUP nuevo. Dice:

```
%VBACKUP-E-NOTSAVESET, File: /mnt/usb/ivan.bck - is not a saveset
```

La caja está bien: ábrela con el VBACKUP nuevo. Si tienes que dar la caja a
alguien con un VBACKUP antiguo, hazla con `/NOSOLID`.

---

## 9. Cerrar la caja con una contraseña

**Para qué:** para que nadie más pueda mirar dentro de la caja.
Por ejemplo, si la memoria USB se pierde o la roban.

**Qué escribir:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT
```

VBACKUP pide la contraseña dos veces:

```
Passphrase for /mnt/usb/ivan.bck:
The same passphrase again:
```

Escribe la contraseña y pulsa Enter. Luego escríbela otra vez, igual.
Mientras escribes, **no se ve nada** en la pantalla, ni siquiera asteriscos. Así debe ser.

**Qué significa:** la caja está cerrada. Sin la contraseña nadie puede mirar
dentro ni sacar archivos. Ni siquiera se ven los nombres de los archivos.

**Abrir una caja cerrada:** igual que antes — ver qué hay dentro (parte 2),
recuperar archivos (partes 3 y 4). No hay que añadir nada:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

VBACKUP ve solo que la caja está cerrada y pregunta:

```
Passphrase for /mnt/usb/ivan.bck:
```

En un ordenador pequeño abrirla tarda un segundo o unos pocos. Es a propósito:
así adivinar la contraseña es muy lento.

**La contraseña es la llave. ¡Muy importante!**

- Si se pierde la contraseña, **NADIE** puede abrir la caja. Ni tú, ni quien te
  preparó el ordenador, ni siquiera el autor de VBACKUP.
- Usa una contraseña larga: cinco o más palabras al azar.
- Escríbela en un papel. Guarda el papel en un sitio seguro.

**¿Guardar sin ti, a una hora fija (cron)? ¿Un gestor de archivos?** Allí nadie escribe la contraseña.
Entonces pon la contraseña en un archivo. Solo cuenta la primera línea.
Y haz el archivo privado — solo tú puedes leerlo:

```
printf 'my long password words here\n' > /root/backup.key
chmod 600 /root/backup.key
```

Ahora da el archivo en vez de escribir:

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT /KEY_FILE=/root/backup.key
```

O dilo una vez, y VBACKUP toma el archivo solo cada vez:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

Para cerrar una caja nueva, escribe `/ENCRYPT` de todos modos.

Los gestores de archivos (MC, far2l, Total Commander, Double Commander) no saben
pedir una contraseña. Abren una caja cerrada solo así, con `VBACKUP_KEY_FILE`.

**vbkx** (sección 12) también abre una caja cerrada. Dale el archivo con `-k`, o te la pide:

```
vbkx x /mnt/usb/ivan.bck -k /root/backup.key
```

**Bueno saber:**

- Una caja cerrada rota se arregla como antes (`BLKFIXED`). Para eso no hace falta la contraseña.
- Si alguien cambió la caja a propósito, VBACKUP lo nota (`BLKFORGED`, sección 13).

**Cuidado:** un VBACKUP antiguo (antes de X01-06) no puede abrir una caja cerrada.
Solo dice que se perdieron bloques y no escribe nada. Instala el VBACKUP nuevo.

---

## 10. Guardar un disco o una partición entera, tal como está

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
%VBACKUP-I-STARTED, Operation: save, Input: /dev/sdb1, Output: /mnt/usb/sdb1.bck - started
%VBACKUP-I-PHYSSUMM, Device: /dev/sdb1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-SAVESUMM, Files: 1, Bytes: 1507328, Blocks: 29, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.35 - completed
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
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/sdb1.bck, Output: /dev/sdc1 - started
Everything on /dev/sdc1 (134217728 bytes) is to be overwritten with the device saved in /mnt/usb/sdb1.bck.
Type YES to go on:
```

Escribe `YES` en mayúsculas y pulsa Enter. Cualquier otra respuesta es «no».

**Qué verás:**

```
%VBACKUP-I-PHYSLARGER, Device: /dev/sdc1, Bytes: 134217728, Saved: 67108864 - larger: the rest stays as it is, the file system keeps its old size
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-PHYSSUMM, Device: /dev/sdc1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.41 - completed
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

## 11. Guardar un sistema de archivos entero y crearlo de nuevo en otro disco

**En qué se diferencia de la sección 10:** la sección 10 copia cada trocito del
disco; aquí VBACKUP copia todos los **archivos** del disco y recuerda qué disco era.
El disco nuevo puede tener otro tamaño.

Esto también lo hace solo **root**.

**Qué escribir** (`/mnt/photos` es donde está conectado el disco):

```
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

**Qué verás:**

```
%VBACKUP-I-STARTED, Operation: save, Input: /mnt/photos, Output: /mnt/usb/photos.bck - started
%VBACKUP-I-SAVESUMM, Files: 11, Bytes: 760000, Blocks: 16, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.09 - completed
```

**Crear el disco de nuevo en `/dev/sdc1`.** ¡Esto **borra todo** en `/dev/sdc1`!

```
vbackup /mnt/usb/photos.bck /dev/sdc1 /IMAGE /REPLACE
```

**Qué verás:**

```
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/photos.bck, Output: /dev/sdc1 - started
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-IMGSUMM, Device: /dev/sdc1, Type: ext4, Files: 11, Bytes: 760000 - file system made, files restored
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 1.27 - completed
```

**Qué significa:** en `/dev/sdc1` se hizo un disco nuevo del mismo tipo (ext4)
con el mismo nombre, y se pusieron en él todos los archivos.

Recuerda:

- hace falta el programa que crea discos de ese tipo (`mkfs.ext4`,
  `mkfs.vfat` …). Lo instala quien te preparó el ordenador;
- así **no** se puede mover el disco con el que arranca el ordenador.
  Para eso guarda el disco entero (`/dev/sdb`, no `/dev/sdb1`) como en la sección 10;
- no conectes el disco viejo y el nuevo a la vez: son gemelos.

---

## 12. ¿No hay VBACKUP? Usa vbkx

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
vbkx: File: ivan/letters/anna.txt - already exists, not extracted (-f to overwrite)
```

Para reemplazarlo, añade `-f`.

---

## 13. Si algo salió mal

Un mensaje se ve así: `%VBACKUP-E-NOMBRE, File: nombre - texto`.
Primero dice de qué se trata (un archivo, una caja, un disco), luego qué pasó.
La letra después de `VBACKUP-` te dice lo grave que es:

- `I` — solo informa. Todo va bien.
- `W` — un aviso. Mira con atención.
- `E` o `F` — un error. Algo no se hizo.

La última línea, `COMPLETED`, dice cómo fue todo: `completed` — todo va bien;
`completed with warnings` — mira con atención; `completed with errors` — algo no se hizo.

### FILEEXISTS

**Qué ves:**

```
%VBACKUP-W-FILEEXISTS, File: /home/ivan/restored/ivan/letters/anna.txt - already exists, not restored
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

### OPENOUT … errno: 17 (File exists)

**Qué ves:**

```
%VBACKUP-E-OPENOUT, File: /mnt/usb/ivan.bck, errno: 17 - cannot be created as output (File exists)
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
%VBACKUP-E-NOTSAVESET, File: /mnt/usb/fake.bck - is not a saveset
```

**Qué pasó:** este archivo no es una caja de VBACKUP.

**Qué hacer:** revisa el nombre. Mira qué hay en la memoria USB:

```
ls /mnt/usb
```

**O bien:** un VBACKUP nuevo hizo la caja más pequeña (sección 8), y tú la
abres con uno antiguo (antes de X01-21). La caja está bien: ábrela con el
VBACKUP nuevo. Para alguien con un VBACKUP antiguo, haz la caja con
`/NOSOLID`.

### OPENIN … errno: 2 (No such file or directory)

**Qué ves:**

```
%VBACKUP-E-OPENIN, File: /mnt/usb/nosuch.bck, errno: 2 - cannot be opened as input (No such file or directory)
```

**Qué pasó:** ese archivo no existe (una errata en el nombre).

**Qué hacer:** revisa el nombre con `ls /mnt/usb`.

### NOPARAM

**Qué ves:**

```
%VBACKUP-E-NOPARAM, Parameter: output specification - is missing
```

**Qué pasó:** pediste `/LIST`, pero el archivo no es una caja; VBACKUP pensó
que querías guardar algo y no dijiste dónde.

**Qué hacer:** revisa el nombre con `ls /mnt/usb`.

### IVOP

**Qué ves:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist - and for a save the output must be named .bck or .sav, or /SAVE_SET given
```

**Qué pasó:** lo primero que escribiste no existe. Seguramente es una errata.

**Qué hacer:** revisa el nombre:

```
ls /home/ivan
```

¿Lo primero es una caja? Entonces su nombre debe terminar en `.bck` o `.sav`.
O añade `/SAVE_SET`.

### MAXPARM

**Qué ves:**

```
%VBACKUP-E-MAXPARM, Parameter: .log - one too many: only an input and an output are taken; a qualifier begins with /
```

**Qué pasó:** demasiadas palabras. Seguramente escribiste `.log` en vez de `/LOG`.

**Qué hacer:** un calificador siempre empieza por `/`:

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG
```

### GLUED

**Qué ves:**

```
%VBACKUP-I-GLUED, Parameter: box.sav/sav - the qualifiers glued to it are taken as qualifiers
```

**Qué pasó:** solo informa. `box.sav/sav` se entendió como `box.sav /SAVE_SET`.

**Qué hacer:** nada. Todo va bien.

### BLKFIXED

**Qué ves:**

```
%VBACKUP-I-BLKFIXED, Block: 3, Volume: 1 - was bad, rebuilt from its group
```

**Qué pasó:** un trocito de la caja estaba roto, pero VBACKUP lo arregló solo.
**Todos los archivos están bien.**

**Qué hacer:** quizá la memoria USB empieza a fallar.
Pronto haz una caja nueva en otra memoria USB.

### BLKFORGED

**Qué ves:**

```
%VBACKUP-W-BLKFORGED, Block: 3, Volume: 1 - is not what was written: its CRC is right, its authentication fails
```

**Qué pasó:** alguien cambió un trocito de una caja cerrada **a propósito**.
Su suma de control parece correcta, pero la cerradura dice: esto no es lo que escribió VBACKUP.

Si justo después sale `BLKFIXED`, el trocito se arregló. **Tus archivos están bien.**

**Qué hacer:** averigua quién pudo escribir en la caja.
Guarda tus cajas donde nadie más pueda cambiarlas.

### BLKLOST y FILDAMAGED

**Qué ves:**

```
%VBACKUP-E-BLKLOST, Block: 3, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, Block: 4, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, File: /home/ivan/restored/ivan/photo1.jpg - is incomplete: its data was lost in bad blocks
```

**Qué pasó:** una parte de la caja está rota y no se pudo arreglar.
Los archivos nombrados en `FILDAMAGED` volvieron incompletos. **Todos los demás están bien.**

**Qué hacer:** saca esos archivos de otra caja, si la tienes.
La próxima vez haz dos cajas en dos memorias USB distintas.

### FILLOST

**Qué ves:**

```
%VBACKUP-E-FILLOST, File: /home/ivan/restored/ivan/photo10.jpg - not restored: its records were lost in bad blocks
```

**Qué pasó:** este archivo se perdió con el trozo roto. No está en absoluto.

**Qué hacer:** sácalo de otra caja más vieja:

```
vbackup /mnt/usb/old.bck /LIST
```

**¿Muchos `FILLOST` a la vez?** Un solo sitio roto, y una lista de
archivos perdidos: la caja se hizo más pequeña (sección 8), y en ese sitio
había un paquete de archivos pequeños. Los archivos del paquete después del
sitio roto se perdieron; los de antes volvieron. Cada archivo perdido tiene
su nombre; todos los demás están bien. Sácalos de otra caja. La próxima vez,
para una caja importante, añade `/PARITY=2`, o `/NOSOLID` para que los
archivos pequeños no se junten en paquetes.

### UNNAMED

**Qué ves:**

```
%VBACKUP-W-UNNAMED, Saveset: /mnt/usb/ivan.bck - blocks were lost and it has no catalog: files missing from the restore cannot all be named
```

**Qué pasó:** la caja está rota y su «índice» también se perdió.
Pueden faltar archivos, pero VBACKUP no puede nombrarlos todos.

**Qué hacer:** compara lo que volvió con la lista de otra caja:

```
vbackup /mnt/usb/old.bck /LIST
```

### NOTINSTREAM

**Qué ves:**

```
%VBACKUP-E-NOTINSTREAM, File: /home/ivan/restored/ivan/notes.txt - not restored: the catalog has it, the record stream does not, though no block was lost
```

**Qué pasó:** el «índice» de la caja nombra este archivo, pero el archivo no
está en la caja, y nada en la caja está roto. Esto no debería pasar nunca.
Quizá la caja la hizo un VBACKUP más nuevo y el tuyo no entiende un trozo de
ella; quizá alguien cambió la caja.

**Qué hacer:** prueba un VBACKUP más nuevo. Saca el archivo de otra caja. Y
avisa a quienes hacen VBACKUP, y dales la caja.

`/COMPARE` y `/VERIFY` cuentan ese archivo como una diferencia
(`COMPARERR … not compared`), así que nunca dicen «todo está bien» cuando
falta un archivo.

### MISSVOL

**Qué ves:**

```
%VBACKUP-E-MISSVOL, Volume: 2, Saveset: /mnt/usb/ivan.bck - is missing
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
%VBACKUP-W-NOTRAILER, Saveset: /mnt/usb/ivan.bck - has no trailer: the save did not complete, or its last volume is missing
```

**Qué pasó:** el guardado no terminó (se fue la luz, el disco se llenó)
o falta el último trozo.

**Qué hacer:** los archivos hasta el corte se pueden sacar como siempre.
Después haz la caja otra vez.

### NOTINCR

**Qué ves:**

```
%VBACKUP-E-NOTINCR, Saveset: /mnt/usb/ivan.bck - it has no catalog: nothing restored with /INCREMENTAL
```

**Qué pasó:** `/INCREMENTAL` necesita una caja entera con «índice». Esta no lo tiene.

**Qué hacer:** sácala sin `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### WRONGKEY

**Qué ves:**

```
%VBACKUP-E-WRONGKEY, Saveset: /mnt/usb/ivan.bck - the passphrase does not open it
```

**Qué pasó:** la contraseña no es la correcta. No se escribió nada.

**Qué hacer:** prueba otra vez, despacio. Revisa las mayúsculas y las minúsculas (¡la tecla **Bloq Mayús**!).
¿La contraseña viene de un archivo de clave? Solo cuenta su primera línea. Mírala:

```
head -1 /root/backup.key
```

### NOKEY

**Qué ves:**

```
%VBACKUP-E-NOKEY, Saveset: /mnt/usb/ivan.bck - needs a passphrase, and there is no terminal to ask it on: give /KEY_FILE=file or VBACKUP_KEY_FILE
```

**Qué pasó:** la caja está cerrada y no hay dónde pedir la contraseña
(cron, un gestor de archivos).

**Qué hacer:** da el archivo de clave (sección 9):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /KEY_FILE=/root/backup.key
```

o dilo una vez:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

### KEYFILE

**Qué ves:**

```
%VBACKUP-E-KEYFILE, Key file: /root/backup.key - others may read or change it - chmod 600 it
```

**Qué pasó:** otras personas pueden leer el archivo de clave. VBACKUP no se fía de él.

**Qué hacer:** hazlo privado:

```
chmod 600 /root/backup.key
```

El mismo mensaje sale si el archivo está vacío o su primera línea es demasiado larga.
Entonces escribe la contraseña en él otra vez (sección 9).

### KEYMATCH

**Qué ves:**

```
%VBACKUP-E-KEYMATCH, the two passphrases differ: nothing saved
```

**Qué pasó:** las dos contraseñas que escribiste no son iguales. No se guardó nada.

**Qué hacer:** prueba otra vez, despacio. Mientras escribes no se ve nada, así que escribe con cuidado.

### PHYSMOUNTED

**Qué ves:**

```
%VBACKUP-E-PHYSMOUNTED, Device: /dev/sdb1 - is mounted read-write on /mnt/photos: unmount it, mount it read-only, or save a snapshot
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
%VBACKUP-E-PHYSHELD, Device: /dev/sdb2 - is in use (swap): free it first, or save what uses it
```

**Qué pasó:** el propio sistema usa este disco (swap, LVM, RAID, cifrado).

**Qué hacer:** pide ayuda a quien te preparó el ordenador. No toques este disco tú solo.

### PHYSREPLACE

**Qué ves:**

```
%VBACKUP-E-PHYSREPLACE, Device: /dev/sdc1 - everything on it would be overwritten: give /REPLACE to do so
```

**Qué pasó:** VBACKUP protege el disco: sin `/REPLACE` no lo borra.

**Qué hacer:** comprueba el nombre del disco (`lsblk`). Si seguro que es ese, añade `/REPLACE`.

### PHYSSMALL

**Qué ves:**

```
%VBACKUP-E-PHYSSMALL, Device: /dev/sdc1, Bytes: 33554432, Saved: 67108864 - too small, nothing written
```

**Qué pasó:** el disco nuevo es más pequeño que el guardado. No cabe todo.

**Qué hacer:** usa un disco más grande. O pon la caja en un archivo imagen (sección 10).

### PHYSABORT

**Qué ves:**

```
%VBACKUP-E-PHYSABORT, Device: /dev/sdc1 - not overwritten: the answer was not YES
```

**Qué pasó:** no contestaste `YES`. No se borró nada.

**Qué hacer:** si de verdad lo quieres, repite y escribe `YES` en mayúsculas.

### IMGNOTVOL

**Qué ves:**

```
%VBACKUP-E-IMGNOTVOL, File: /home/ivan - is neither the mount point of a file system nor a device: /IMAGE saves a whole volume
```

**Qué pasó:** `/IMAGE` guarda un disco entero, y tú diste una carpeta normal.

**Qué hacer:** da el sitio donde está conectado el disco (por ejemplo `/mnt/photos`),
o guarda la carpeta de la forma normal (sección 1).

### IMGNOTMNT

**Qué ves:**

```
%VBACKUP-E-IMGNOTMNT, Device: /dev/sdb1 - is not mounted: mount it (read-only is enough) and give the mount point or the device
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
%VBACKUP-E-IMGUNSUPP, Saveset: /mnt/usb/old.bck, Type: minix - VBACKUP does not make such a file system: use /PHYSICAL for it
```

**Qué pasó:** VBACKUP no sabe crear un disco de ese tipo.

**Qué hacer:** guarda ese disco como en la sección 10 (`/PHYSICAL`).
O saca de la caja solo los archivos (sección 3).

### IMGMKFS

**Qué ves** (por ejemplo):

```
%VBACKUP-E-IMGMKFS, Command: mkfs.xfs -f -q -L PHOTOS /dev/sdc1 - failed: the program is not installed
```

**Qué pasó:** no se pudo crear el disco nuevo. Casi siempre falta el programa.

**Qué hacer:** pide a quien te preparó el ordenador que lo instale (aquí, `mkfs.xfs`).

### IMGSMALL

**Qué ves:**

```
%VBACKUP-E-IMGSMALL, Device: /dev/sdc1, Bytes: 8388608, Needed: 17596518 - too small, nothing written
```

**Qué pasó:** los archivos no caben en este disco.

**Qué hacer:** usa un disco más grande.

### REMOTE

**Qué ves** (por ejemplo):

```
%VBACKUP-E-REMOTE, Node: backup-host, errno: 11 - the pipe to VBACKUP there cannot be made (Resource temporarily unavailable)
```

**Qué pasó:** VBACKUP no pudo ni empezar a hablar con el otro ordenador.
El problema está en **este** ordenador: está demasiado ocupado, hay demasiados programas.
No se guardó ni se sacó nada.

**Qué hacer:** espera un poco y prueba otra vez. Si vuelve a pasar, pide ayuda a tu administrador.

### REMOTEERR

**Qué ves:**

```
%VBACKUP-E-REMOTEERR, Node: backup-host, Exit: 2 - VBACKUP there did not complete: see its messages above
```

**Qué pasó:** el trabajo con el otro ordenador no terminó.
Por qué — está escrito **en las líneas justo encima** de esta.

**Qué hacer:** lee las líneas de arriba. Por ejemplo:

- `Could not resolve hostname` — no hay ningún ordenador con ese nombre. Revisa el nombre.
- `Permission denied` — el otro ordenador no te deja entrar.
  Pide a tu administrador que configure las claves ssh.
- `command not found` — en el otro ordenador no hay VBACKUP.
  Hace falta VBACKUP X01-11 o más nuevo.
- algo sobre `/TRANSFER` — el VBACKUP de allí es demasiado antiguo. Hace falta X01-11 o más nuevo.
- `OPENOUT … File exists` — allí ya hay una caja con ese nombre.
  Pon otro nombre, o añade `/REPLACE`.
- ninguna línea arriba, y al final `Exit: 127` — aquí falta el programa `ssh`.
  Pide ayuda a tu administrador.

### BLKCOPIED

**Qué ves:**

```
%VBACKUP-W-BLKCOPIED, Block: 39, Volume: 2 - is bad, copied as it is: a restore repairs it from its group
```

**Qué pasó:** mientras se copiaba una caja, un trocito de ella estaba roto.
VBACKUP lo copió tal cual. **Tus archivos están bien**: cuando los saques,
VBACKUP arregla ese trocito solo (`BLKFIXED`).

**Qué hacer:** quizá el disco o la memoria USB con la caja vieja empieza a fallar.
Guarda la copia nueva y pronto haz una caja nueva con tus archivos.

---

## 14. Ayuda, no entiendo nada

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

## 15. Una caja de un viejo ordenador OpenVMS

**Para qué:** tienes una caja hecha por BACKUP en un viejo ordenador
con OpenVMS (un VAX o un Alpha). Suele llamarse algo como `USERS.BCK`.
VBACKUP también puede abrirla.

**Primero copia la caja aquí en modo binario.** Con FTP, escribe `binary`
antes de `get`:

```
ftp> binary
ftp> get DKA0:[BACKUP]USERS.BCK USERS.BCK
```

**Ver qué hay dentro** — la misma lista que muestra OpenVMS:

```
vbackup USERS.BCK /LIST
```

**Sacarlo todo a una carpeta:**

```
vbackup USERS.BCK /home/ivan/vms
```

Los nombres cambian un poco: `[SMITH.WORK]NOTES.TXT;5` pasa a ser
`SMITH/WORK/NOTES.TXT`. Si hay versiones antiguas, conservan el número:
`NOTES.TXT;4`. Los ficheros de texto pasan a ser ficheros de texto normales.

**Sacar un solo fichero:**

```
vbackup USERS.BCK /EXTRACT=SMITH/LOGIN.COM login.com
```

Si ves `VMSRAW` — ese fichero es un fichero especial de OpenVMS (una base
de datos). Se copió tal cual; solo OpenVMS puede leerlo.

---

## 16. VBACKUP en un ordenador con Windows

**Para qué:** tienes un ordenador con Windows y quieres hacer cajas allí,
o abrir una caja hecha en Linux.

Existe `vbackup.exe` para Windows. Es el mismo VBACKUP: las mismas
órdenes, las mismas cajas. Una caja hecha en Linux se abre en Windows, y
una caja hecha en Windows se abre en Linux.

**Meter una carpeta en una caja:**

```
C:\> vbackup C:\Users\ivan\Documents D:\docs.bck
```

**Ver qué hay dentro y sacarlo todo:**

```
C:\> vbackup D:\docs.bck /LIST
C:\> vbackup D:\docs.bck C:\restore
```

**Guardarlo todo, con los dueños.** Ejecuta `vbackup.exe` como
administrador (clic derecho en "Símbolo del sistema", "Ejecutar como
administrador"). Así puede leer todos los archivos y, cuando los saques,
vuelven a tener sus dueños y sus permisos. Si no, los archivos que sacas
pasan a ser tuyos.

**Algunos nombres no pueden vivir en Windows.** En Linux un archivo puede
llamarse `a:b`, `qué?` o `con.txt`. Windows no permite esos nombres.
VBACKUP no crea esos archivos y te lo dice:

```
%VBACKUP-E-OPENOUT, File: tree/a:b, errno: 22 - cannot be created as output (not a valid name on Windows)
```

Todos los demás archivos salen. Para sacar uno de esos, abre la caja en Linux.

**Mayúsculas y minúsculas.** En Linux `README` y `readme` son dos archivos.
En Windows son uno. VBACKUP saca el primero y te avisa del segundo.
Nunca escribe uno encima del otro.

**Lo que Windows no puede hacer:** `/PHYSICAL` y `/IMAGE`. Usa Linux para eso.

---

## Pequeño diccionario

- **Carpeta** — un sitio donde viven los archivos. Como un cajón de un armario.
- **Archivo** — una carta, una foto, una canción.
- **Orden** — una línea que escribes en el terminal y envías con la tecla Enter.
- **Terminal** — la ventana donde escribes las órdenes.
- **Saveset (caja)** — un archivo grande que guarda todos tus archivos. Su nombre termina en `.bck` o `.sav`.
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
- **Calificador** — una palabra con `/` delante, como `/LOG`. Le dice a VBACKUP cómo trabajar.
- **Contraseña (passphrase)** — las palabras secretas que cierran y abren una caja. Como la llave de una puerta:
  si la pierdes, la puerta queda cerrada para siempre.
- **Archivo de clave** — un archivo pequeño con la contraseña en su primera línea. Solo tú puedes leerlo (`chmod 600`).
- **Caja cerrada (cifrada)** — una caja hecha con `/ENCRYPT`. Sin la contraseña nadie puede mirar dentro,
  ni siquiera los nombres de los archivos.
- **Nodo (`nombre-del-ordenador::`)** — otro ordenador, en el nombre de una caja: `backup-host::/backup/ivan.bck`
  quiere decir «la caja `/backup/ivan.bck` en el ordenador `backup-host`». Dos veces dos puntos, sin espacios.
- **Claves ssh** — una forma segura de que un ordenador te deje entrar en otro sin escribir contraseña.
  Las configura tu administrador.
