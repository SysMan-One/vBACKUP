# VBACKUP — очень просто

VBACKUP складывает твои файлы в одну большую «коробку».
Коробку можно положить на флешку.
Потом из коробки можно достать всё обратно.

```
   твоя папка              коробка (saveset)            флешка
  +-----------+            +--------------+            +-------+
  | /home/ivan|  ------->  |  ivan.bck    |  ------->  | /mnt/ |
  |  письма   |  vbackup   | [][][][][][] |            |  usb  |
  |  фото     |            +--------------+            +-------+
  +-----------+
```

---

## Три главные команды

Положить файлы в коробку:

```
vbackup /home/ivan /mnt/usb/ivan.bck
```

Посмотреть, что в коробке:

```
vbackup /mnt/usb/ivan.bck /LIST
```

Достать всё обратно (в новую папку):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

Это всё, что нужно знать. Дальше — подробно и по шагам.

И ещё: сделать коробку меньше — раздел 7; сохранить целый диск — разделы 8 и 9.

---

## Перед тем как начать

1. Открой окно для команд. Оно называется «терминал».
2. Напечатай команду точно так, как написано здесь.
3. Нажми клавишу **Enter**.

Важные правила:

- Имя коробки **всегда** кончается на `.bck`. Например: `ivan.bck`.
  Если забыть `.bck`, VBACKUP не сделает коробку, а просто скопирует папку.
- Пробелы между частями команды нужны. Не пропускай их.
- Большие и маленькие буквы важны: `/home/ivan` и `/Home/Ivan` — это разное.
- Если ничего не написано в ответ — это хорошо. Значит, всё получилось.

В примерах:

- `/home/ivan` — твоя папка с файлами.
- `/mnt/usb` — флешка.
- `/mnt/disk2` — второй диск.

Поставь вместо них свои имена.

---

## 1. Сохранить мои файлы

**Зачем:** чтобы у тебя была копия, если компьютер сломается.

**Что набрать:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG /VERIFY
```

`/LOG` — показывать каждый файл. `/VERIFY` — сразу проверить коробку.
Можно и без них, тогда VBACKUP просто молча работает.

**Что увидишь:**

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

**Что это значит:**

- Начало строки — дата, время и номер. На него можно не смотреть.
- `saved` — файл лёг в коробку.
- `6 files ... saved` — всего сохранено 6 штук.
- `0 differences` — коробка проверена, всё совпадает. Отлично!

**Флешка маленькая или старая (FAT32)?** Разрежь коробку на куски по 4 ГБ:

```
vbackup /home/ivan /mnt/usb/ivan.bck /VOLUME_SIZE=4G
```

Куски будут называться `ivan.bck`, `ivan.bck.002`, `ivan.bck.003`…
Храни их всегда вместе, в одной папке.

---

## 2. Посмотреть, что внутри

**Зачем:** чтобы узнать, что лежит в коробке, ничего не доставая.

**Что набрать:**

```
vbackup /mnt/usb/ivan.bck /LIST
```

**Что увидишь (конец списка):**

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

**Что это значит:**

- Каждая строка — один файл. Если в конце `/` — это папка.
- Число — размер файла. Дальше — когда файл последний раз меняли.
- Сверху ещё будет «шапка»: когда и кто сделал коробку.

---

## 3. Вернуть файлы обратно

**Зачем:** файлы пропали или испортились — достаём их из коробки.

**Что набрать:**

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /LOG
```

**Что увидишь:**

```
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/letters/anna.txt restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/cat.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTORED, /home/ivan/restored/ivan/photos/dog.jpg restored
04-10-2026 12:59:40.265 2279186 %VBACKUP-I-RESTSUMM, 6 files, 50010 bytes restored
```

**Что это значит:**

- `restored` — файл вернулся.
- Файлы лежат в новой папке `/home/ivan/restored/ivan/…`.
  Посмотри их там спокойно. Ничего старого не испорчено.

Совет: всегда доставай в **новую пустую** папку. Так безопаснее.

---

## 4. Вернуть один файл

**Зачем:** нужен только один файл, а не все.

Сначала посмотри точное имя файла командой из раздела 2.
Например: `ivan/letters/anna.txt`.

**Что набрать:**

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt /home/ivan/anna.txt
```

**Что увидишь:** ничего. Это хорошо. Файл уже лежит в `/home/ivan/anna.txt`.

Можно просто посмотреть файл на экране, не сохраняя:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
Dear Anna
```

Если имя написано неправильно, увидишь:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, ivan/letters/nope.txt is not in the saveset
```

Значит: такого файла в коробке нет. Проверь имя по списку (раздел 2).

---

## 5. Сохранять только новое (каждый день)

**Зачем:** чтобы не копировать каждый день всё заново, а только то, что изменилось.

**Шаг 1. Один раз — большая коробка со всем:**

```
vbackup /home/ivan /mnt/usb/full.bck /RECORD
```

```
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, 3 files recorded in the journal /var/lib/vbackup/vbackup.jnl
```

`/RECORD` — запомнить, что уже сохранено. VBACKUP ведёт для этого тетрадку — «журнал».

**Шаг 2. Каждый день — маленькая коробка только с новым:**

```
vbackup /home/ivan /mnt/usb/mon.bck /SINCE=BACKUP /RECORD
```

```
04-10-2026 13:00:03.289 2284360 %VBACKUP-I-RECORDED, 1 file recorded in the journal /var/lib/vbackup/vbackup.jnl
```

Журнал — это тетрадка, где VBACKUP помнит, что уже сохранено. У root она
лежит в `/var/lib/vbackup/vbackup.jnl`, у обычного пользователя — в
`~/.vbackup/vbackup.jnl` (в твоей домашней папке). Сам её не трогай.

На следующий день — другое имя: `tue.bck`, потом `wed.bck` и так далее.

**Вернуть всё из цепочки коробок:** перечисли их через запятую,
**от самой старой к самой новой**, и добавь `/INCREMENTAL`:

```
vbackup /mnt/usb/full.bck,/mnt/usb/mon.bck,/mnt/usb/tue.bck /home/ivan/restored /INCREMENTAL /LOG
```

**Что увидишь (конец):**

```
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, /home/ivan/restored/ivan/photos/dog.jpg deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, 13 files, 50021 bytes restored
```

**Что это значит:**

- Файлы вернутся такими, какими были в день последней коробки.
- `deleted` — этот файл ты удалил во вторник, поэтому его и нет. Так и надо.

Запомни: пробелов после запятых нет.

---

## 6. Скопировать папку на другой диск

**Зачем:** просто сделать копию папки, без коробки.

**Что набрать** (имя **без** `.bck`):

```
vbackup /home/ivan /mnt/disk2 /VERIFY
```

**Что увидишь:** ничего. Это хорошо. Папка теперь есть и тут: `/mnt/disk2/ivan`.

С `/LOG` VBACKUP покажет каждый файл:

```
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, /mnt/disk2/ivan/photos/cat.jpg copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, 6 files, 20021 bytes copied
```

`copied` — файл скопирован.

---

## 7. Сделать коробку меньше

**Зачем:** чтобы коробка занимала меньше места на флешке.

**Что набрать:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /DATA_FORMAT=COMPRESSED
```

**Что увидишь:** ничего. Это хорошо. Коробка готова, и она меньше.

Сравни: та же папка без сжатия и со сжатием:

```
без сжатия:  1048576 байт
со сжатием:   655360 байт
```

**Что это значит:** письма и документы сжимаются хорошо.
Фото, видео, архивы (`.jpg`, `.mp4`, `.zip`) уже сжаты — их VBACKUP
кладёт как есть. Ничего не ломается.

Доставать сжатую коробку — как обычно, ничего добавлять не надо:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

**Внимание:** старая программа VBACKUP (до X01-04) сжатую коробку не
понимает. Она скажет, что файлы повреждены:

```
%VBACKUP-E-CRCERR, /home/ivan/restored/ivan/letters/letter1.txt: checksum mismatch, the data differ from what was saved
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/letters/letter1.txt is incomplete: its data was lost in bad blocks
```

Файлы в коробке целы. Просто поставь новую версию VBACKUP.

---

## 8. Сохранить целый диск или раздел, как он есть

**Зачем:** сделать точную копию всего диска — каждый его кусочек.
Так копируют диск, с которого включается компьютер, или зашифрованный диск.

Это делает только главный пользователь компьютера — **root**.

**Сначала узнай имя диска.** Это очень важно:

```
lsblk
```

Ты увидишь список дисков, например `sdb`, а на нём раздел `sdb1`.
Полное имя раздела: `/dev/sdb1`.

**Прежде чем сохранять:** на этот диск никто не должен писать.
Отключи его (`umount`) или подключи «только для чтения».
Иначе копия получится испорченной, и ты этого не заметишь.

**Что набрать:**

```
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

**Что увидишь:**

```
%VBACKUP-I-PHYSSUMM, /dev/sdb1: 67108864 bytes, 1507328 of them data, the rest zeros
```

**Что это значит:** диск размером 64 МБ сохранён. Настоящих данных на нём
было 1,5 МБ. Пустые места (нули) в коробке места не занимают.

**Вернуть на диск.** Это **сотрёт всё**, что сейчас на диске `/dev/sdc1`!
Проверь имя командой `lsblk` два раза.

```
vbackup /mnt/usb/sdb1.bck /dev/sdc1 /PHYSICAL /REPLACE
```

VBACKUP спросит:

```
Everything on /dev/sdc1 (134217728 bytes) is to be overwritten with the device saved in /mnt/usb/sdb1.bck.
Type YES to go on:
```

Напечатай большими буквами `YES` и нажми Enter. Любой другой ответ — отказ.

**Что увидишь:**

```
%VBACKUP-I-PHYSLARGER, /dev/sdc1 holds 134217728 bytes, the device saved held 67108864: the rest stays as it is, the file system keeps its old size
%VBACKUP-I-PHYSUUID, /dev/sdc1 now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-PHYSSUMM, /dev/sdc1: 67108864 bytes, 1507328 of them data, the rest zeros
```

**Что это значит:**

- новый диск больше старого — копия легла в начало, остальное не тронуто;
- копия — двойник старого диска. **Не подключай** старый и новый диск
  к компьютеру одновременно: они перепутаются;
- диск меньше сохранённого не подойдёт — VBACKUP откажется.

**Вернуть в файл-образ** (ничего не стирает):

```
vbackup /mnt/usb/sdb1.bck /home/ivan/sdb1.img /PHYSICAL
```

---

## 9. Сохранить всю файловую систему и создать её заново на другом диске

**Чем отличается от раздела 8:** раздел 8 копирует каждый кусочек диска,
а здесь VBACKUP копирует все **файлы** диска и запоминает, какой это был диск.
Новый диск может быть другого размера.

Это тоже делает только **root**.

**Что набрать** (здесь `/mnt/photos` — место, куда подключён диск):

```
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

**Что увидишь:** ничего. Это хорошо.

**Создать диск заново на `/dev/sdc1`.** Это **сотрёт всё** на `/dev/sdc1`!

```
vbackup /mnt/usb/photos.bck /dev/sdc1 /IMAGE /REPLACE
```

**Что увидишь:**

```
%VBACKUP-I-PHYSUUID, /dev/sdc1 now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-IMGSUMM, /dev/sdc1: a ext4 file system made, 11 files, 760000 bytes restored
```

**Что это значит:** на `/dev/sdc1` сделан новый диск того же типа (ext4)
с тем же именем, и в него положены все файлы.

Помни:

- для этого нужна программа, которая делает диски нужного типа
  (`mkfs.ext4`, `mkfs.vfat` …). Её ставит тот, кто ставит тебе компьютер;
- так **нельзя** перенести диск, с которого включается компьютер.
  Для этого сохрани весь диск (`/dev/sdb`, не `/dev/sdb1`) способом из раздела 8;
- не подключай старый и новый диск одновременно — они двойники.

---

## 10. Если VBACKUP нет — vbkx

**Зачем:** ты на другом компьютере, а там VBACKUP не установлен.
Но есть программа `vbkx` — одна, маленькая. Принеси её на той же флешке.

**Посмотреть, что в коробке:**

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

Буква `d` — папка, `-` — обычный файл.

**Достать всё в папку:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored
```

Ничего не написано — значит, получилось.

**Достать только одну папку:**

```
vbkx x /mnt/usb/ivan.bck -C /home/ivan/restored ivan/photos
```

**Показать один файл на экране:**

```
vbkx p /mnt/usb/ivan.bck ivan/letters/anna.txt
```

```
Dear Anna
```

**Проверить, цела ли коробка:**

```
vbkx t /mnt/usb/ivan.bck
```

```
/mnt/usb/ivan.bck: all files read, all checksums match
```

Значит: коробка целая.

Если файл уже есть, `vbkx` его не трогает и говорит:

```
vbkx: ivan/letters/anna.txt exists, not extracted (-f to overwrite)
```

Хочешь заменить — добавь `-f`.

---

## 11. Если что-то пошло не так

Сообщение выглядит так: `%VBACKUP-E-ИМЯ, текст`.
Буква после `VBACKUP-` подсказывает, насколько всё серьёзно:

- `I` — просто сообщает. Всё хорошо.
- `W` — предупреждает. Посмотри внимательно.
- `E` или `F` — ошибка. Что-то не сделано.

### FILEEXISTS

**Что ты видишь:**

```
%VBACKUP-W-FILEEXISTS, /home/ivan/restored/ivan/letters/anna.txt already exists, not restored
```

**Что случилось:** там уже лежит такой файл. VBACKUP его бережёт и не трогает.

**Что сделать:** доставай в новую пустую папку:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored2
```

или замени старые файлы (осторожно!):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /REPLACE
```

### OPENOUT … errno=17 (File exists)

**Что ты видишь:**

```
%VBACKUP-E-OPENOUT, error creating /mnt/usb/ivan.bck as output, errno=17 (File exists)
```

**Что случилось:** коробка с таким именем уже есть.

**Что сделать:** дай другое имя:

```
vbackup /home/ivan /mnt/usb/ivan2.bck
```

или перепиши старую коробку (старая пропадёт!):

```
vbackup /home/ivan /mnt/usb/ivan.bck /REPLACE
```

### NOTSAVESET

**Что ты видишь:**

```
%VBACKUP-E-NOTSAVESET, /mnt/usb/fake.bck is not a saveset
```

**Что случилось:** этот файл — не коробка VBACKUP.

**Что сделать:** проверь имя. Посмотри, что лежит на флешке:

```
ls /mnt/usb
```

### NOPARAM

**Что ты видишь:**

```
%VBACKUP-E-NOPARAM, missing parameter: input specification - it does not exist
```

или

```
%VBACKUP-E-NOPARAM, missing parameter: output specification
```

**Что случилось:** в первом случае — такого файла нет (опечатка в имени).
Во втором — ты просил `/LIST`, но файл не коробка, и VBACKUP решил, что ты хочешь
что-то сохранить, а куда — не сказано.

**Что сделать:** проверь имя командой `ls /mnt/usb`.

### IVOP

**Что ты видишь:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist
```

**Что случилось:** того, что ты написал первым, нет. Скорее всего, опечатка.

**Что сделать:** проверь имя:

```
ls /home/ivan
```

### BLKFIXED

**Что ты видишь:**

```
%VBACKUP-I-BLKFIXED, block 3 of volume 1 was bad and has been rebuilt from its group
```

**Что случилось:** кусочек коробки был испорчен, но VBACKUP его сам починил.
**Все файлы целы.**

**Что сделать:** флешка, может быть, начинает портиться.
Скоро сделай новую коробку на другой флешке.

### BLKLOST и FILDAMAGED

**Что ты видишь:**

```
%VBACKUP-E-BLKLOST, block 3 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, block 4 of volume 1 is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, /home/ivan/restored/ivan/photo1.jpg is incomplete: its data was lost in bad blocks
```

**Что случилось:** часть коробки испорчена, починить не удалось.
Файлы, названные в `FILDAMAGED`, вернулись не целиком. **Все остальные — целы.**

**Что сделать:** эти файлы возьми из другой коробки, если она есть.
В следующий раз делай две коробки на разных флешках.

### FILLOST

**Что ты видишь:**

```
%VBACKUP-E-FILLOST, /home/ivan/restored/ivan/photo10.jpg was not restored: its records were lost in bad blocks
```

**Что случилось:** этот файл пропал вместе с испорченным кусочком. Его вообще нет.

**Что сделать:** возьми его из другой, более старой коробки:

```
vbackup /mnt/usb/old.bck /LIST
```

### UNNAMED

**Что ты видишь:**

```
%VBACKUP-W-UNNAMED, /mnt/usb/ivan.bck: blocks were lost and it has no catalog - files missing from the restore cannot all be named
```

**Что случилось:** коробка испорчена, и её «оглавление» тоже пропало.
Какие-то файлы могли потеряться, но VBACKUP не может назвать их все.

**Что сделать:** сравни, что вернулось, со списком из другой коробки:

```
vbackup /mnt/usb/old.bck /LIST
```

### MISSVOL

**Что ты видишь:**

```
%VBACKUP-E-MISSVOL, volume 2 of /mnt/usb/ivan.bck is missing
```

**Что случилось:** коробка была разрезана на куски, и одного куска нет
(например, `ivan.bck.002`).

**Что сделать:** положи все куски в одну папку, имена не меняй, и повтори:

```
ls /mnt/usb
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### NOTRAILER

**Что ты видишь:**

```
%VBACKUP-W-NOTRAILER, /mnt/usb/ivan.bck has no trailer: the save did not complete, or its last volume is missing
```

**Что случилось:** сохранение не закончилось (выключили свет, кончилось место)
или нет последнего куска.

**Что сделать:** файлы до места обрыва достать можно — это делается как обычно.
Потом сделай коробку заново.

### NOTINCR

**Что ты видишь:**

```
%VBACKUP-E-NOTINCR, /mnt/usb/ivan.bck: it has no catalog, nothing restored with /INCREMENTAL
```

**Что случилось:** для `/INCREMENTAL` нужна целая коробка с «оглавлением». У этой его нет.

**Что сделать:** достань её без `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### PHYSMOUNTED

**Что ты видишь:**

```
%VBACKUP-E-PHYSMOUNTED, /dev/sdb1 is mounted read-write on /mnt/photos: unmount it, mount it read-only, or save a snapshot
```

**Что случилось:** диск подключён, и на него можно писать. Копия была бы испорчена.

**Что сделать:** отключи диск и повтори:

```
umount /mnt/photos
vbackup /dev/sdb1 /mnt/usb/sdb1.bck /PHYSICAL
```

При восстановлении то же сообщение значит: на подключённый диск VBACKUP
ничего не пишет. Отключи его.

### PHYSHELD

**Что ты видишь:**

```
%VBACKUP-E-PHYSHELD, /dev/sdb2 is in use (swap): free it first, or save what uses it
```

**Что случилось:** диском пользуется сама система (swap, LVM, RAID, шифрование).

**Что сделать:** попроси того, кто ставил компьютер. Сам этот диск не трогай.

### PHYSREPLACE

**Что ты видишь:**

```
%VBACKUP-E-PHYSREPLACE, /dev/sdc1 is a device: everything on it is overwritten - give /REPLACE to do so
```

**Что случилось:** VBACKUP бережёт диск: без `/REPLACE` он его не стирает.

**Что сделать:** проверь имя диска (`lsblk`). Если точно он — добавь `/REPLACE`.

### PHYSSMALL

**Что ты видишь:**

```
%VBACKUP-E-PHYSSMALL, /dev/sdc1 holds 33554432 bytes, the device saved held 67108864: nothing written
```

**Что случилось:** новый диск меньше сохранённого. Всё не поместится.

**Что сделать:** возьми диск побольше. Или верни коробку в файл-образ (раздел 8).

### PHYSABORT

**Что ты видишь:**

```
%VBACKUP-E-PHYSABORT, /dev/sdc1 not overwritten: the answer was not YES
```

**Что случилось:** ты ответил не `YES`. Ничего не стёрто.

**Что сделать:** если правда хочешь — повтори и напечатай `YES` большими буквами.

### IMGNOTVOL

**Что ты видишь:**

```
%VBACKUP-E-IMGNOTVOL, /home/ivan is neither the mount point of a file system nor a device: /IMAGE saves a whole volume
```

**Что случилось:** `/IMAGE` сохраняет диск целиком, а ты дал обычную папку.

**Что сделать:** дай место, куда подключён диск (например `/mnt/photos`),
или сохрани папку обычным способом (раздел 1).

### IMGNOTMNT

**Что ты видишь:**

```
%VBACKUP-E-IMGNOTMNT, /dev/sdb1 is not mounted: mount it (read-only is enough) and give the mount point or the device
```

**Что случилось:** диск не подключён — VBACKUP не может прочитать файлы.

**Что сделать:** подключи его только для чтения и повтори:

```
mount -o ro /dev/sdb1 /mnt/photos
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

### IMGUNSUPP

**Что ты видишь:**

```
%VBACKUP-E-IMGUNSUPP, /mnt/usb/old.bck: VBACKUP does not make a file system of type minix - use /PHYSICAL for it
```

**Что случилось:** такой тип диска VBACKUP делать не умеет.

**Что сделать:** сохрани такой диск способом из раздела 8 (`/PHYSICAL`).
Или достань из коробки только файлы (раздел 3).

### IMGMKFS

**Что ты видишь** (например):

```
%VBACKUP-E-IMGMKFS, mkfs.xfs -f -q -L PHOTOS /dev/sdc1 failed: the program is not installed
```

**Что случилось:** не получилось сделать новый диск. Чаще всего нет нужной программы.

**Что сделать:** попроси того, кто ставил компьютер, установить её (здесь — `mkfs.xfs`).

### IMGSMALL

**Что ты видишь:**

```
%VBACKUP-E-IMGSMALL, /dev/sdc1 holds 8388608 bytes, the files need about 17596518: nothing written
```

**Что случилось:** файлы не поместятся на этот диск.

**Что сделать:** возьми диск побольше.

---

## 12. Помогите, ничего не понимаю

Это нормально. Попроси помощи у самой программы.

Короткая подсказка:

```
vbackup
```

Полная справка на английском (дальше — клавиша **Enter**, выход — клавиша **Q**):

```
vbackup /HELP
```

Что делать при ошибках:

```
vbackup /HELP TROUBLESHOOTING
```

Справка на русском:

```
vhelp VBACKUP /LIBRARY=/usr/local/share/help/ru/vbackup_ru.hlb
```

Руководство (выход — клавиша **q**):

```
man vbackup
man vbkx
```

И главное: попроси помочь того, кто ставил тебе компьютер. Покажи ему сообщение.

---

## Словарик

- **Папка** — место, где лежат файлы. Как ящик в шкафу.
- **Файл** — одно письмо, одна фотография, одна песня.
- **Команда** — строчка, которую ты печатаешь в терминале и отправляешь клавишей Enter.
- **Терминал** — окно, куда печатают команды.
- **Saveset (коробка)** — один большой файл, в котором лежат все твои файлы. Имя кончается на `.bck`.
- **Том (кусок)** — часть разрезанной коробки: `ivan.bck.002`, `ivan.bck.003`.
- **Флешка** — маленький диск, который вставляют в компьютер.
- **Журнал** — тетрадка VBACKUP, где записано, что уже сохранено.
- **Блок** — маленький кусочек коробки. Если один испорчен, VBACKUP чинит его сам.
- **Оглавление (каталог)** — список всех файлов в конце коробки.
- **Раздел** — часть диска. Один диск можно поделить на несколько разделов: `sdb1`, `sdb2`.
- **Файловая система** — порядок, в котором файлы лежат на диске. У неё есть тип: ext4, vfat …
- **Точка монтирования** — папка, через которую видно подключённый диск. Например `/mnt/photos`.
- **Образ** — один файл, в котором лежит весь диск, кусочек за кусочком.
- **UUID** — длинный номер диска, как паспорт. У копии он такой же, как у оригинала.
