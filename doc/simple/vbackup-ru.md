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

И ещё: сделать коробку меньше — раздел 7; запереть её паролем — раздел 8;
сохранить целый диск — разделы 9 и 10.

---

## Перед тем как начать

1. Открой окно для команд. Оно называется «терминал».
2. Напечатай команду точно так, как написано здесь.
3. Нажми клавишу **Enter**.

Важные правила:

- Имя коробки **всегда** кончается на `.bck` или `.sav`. Например: `ivan.bck`.
  Если это забыть, VBACKUP не сделает коробку, а просто скопирует папку.
  (Другое имя? Тогда добавь `/SAVE_SET`.)
- Слово вроде `/LOG` или `/LIST` — это «квалификатор». Он **всегда** начинается с `/`.
  Если напечатать `.log` вместо `/LOG`, VBACKUP скажет `MAXPARM` и ничего не сделает.
- Квалификатор можно и приклеить к имени: `box.sav/sav/log` тоже работает. VBACKUP тогда скажет `GLUED`.
- Пробелы между частями команды нужны. Не пропускай их.
- Большие и маленькие буквы важны: `/home/ivan` и `/Home/Ivan` — это разное.
- В конце VBACKUP пишет `completed`. Это хорошо. Значит, всё получилось.

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
Можно и без них, тогда VBACKUP покажет только начало, итог и конец.

**Что увидишь:**

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

**Что это значит:**

- Начало строки — дата, время и номер. На него можно не смотреть.
- `started` и `completed` — работа началась и закончилась. `completed` — значит, всё хорошо.
- `saved` — файл лёг в коробку.
- `Files: 6 ... saved` — всего сохранено 6 штук.
- `Differences: 0` — коробка проверена, всё совпадает. Отлично!

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

**Что увидишь:**

```
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck, Output: /home/ivan/anna.txt - started
04-10-2026 12:59:45.611 2281302 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

Это хорошо. Файл уже лежит в `/home/ivan/anna.txt`.

Можно просто посмотреть файл на экране, не сохраняя:

```
vbackup /mnt/usb/ivan.bck /EXTRACT=ivan/letters/anna.txt
```

```
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-STARTED, Operation: extract, Input: /mnt/usb/ivan.bck - started
Dear Anna
04-10-2026 12:59:47.204 2281449 %VBACKUP-I-COMPLETED, Operation: extract, Seconds: 0.00 - completed
```

Если имя написано неправильно, увидишь:

```
04-10-2026 12:59:49.082 2281596 %VBACKUP-E-NOTFOUND, File: ivan/letters/nope.txt - is not in the saveset
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
04-10-2026 13:00:01.548 2284063 %VBACKUP-I-STARTED, Operation: save, Input: /home/ivan, Output: /mnt/usb/full.bck - started
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-SAVESUMM, Files: 4, Bytes: 1842, Blocks: 4, Volumes: 1 - saved
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-RECORDED, Files: 3, Journal: /var/lib/vbackup/vbackup.jnl - recorded
04-10-2026 13:00:01.552 2284063 %VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.01 - completed
```

`/RECORD` — запомнить, что уже сохранено. VBACKUP ведёт для этого тетрадку — «журнал».

**Шаг 2. Каждый день — маленькая коробка только с новым:**

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
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-DELETED, File: /home/ivan/restored/ivan/photos/dog.jpg - deleted: it is not in the incremental saveset
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-RESTSUMM, Files: 13, Bytes: 50021 - restored
04-10-2026 13:00:05.792 2284835 %VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.02 - completed
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

**Что увидишь:**

```
04-10-2026 13:00:12.410 2285890 %VBACKUP-I-STARTED, Operation: copy, Input: /home/ivan, Output: /mnt/disk2 - started
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:12.418 2285890 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

Это хорошо. Папка теперь есть и тут: `/mnt/disk2/ivan`.

С `/LOG` VBACKUP покажет каждый файл:

```
...
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COPIED, File: /mnt/disk2/ivan/photos/cat.jpg - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-CPYSUMM, Files: 6, Bytes: 20021 - copied
04-10-2026 13:00:14.232 2286104 %VBACKUP-I-COMPLETED, Operation: copy, Seconds: 0.01 - completed
```

`copied` — файл скопирован.

---

## 7. Сделать коробку меньше

**Зачем:** чтобы коробка занимала меньше места на флешке.

**Что набрать:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /DATA_FORMAT=COMPRESSED
```

**Что увидишь:** начало, итог и `completed`, как в части 1. Коробка готова, и она меньше.

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

## 8. Запереть коробку паролем

**Зачем:** чтобы никто чужой не мог заглянуть в коробку.
Например, если флешку потеряли или украли.

**Что набрать:**

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT
```

VBACKUP два раза спросит пароль:

```
Passphrase for /mnt/usb/ivan.bck:
The same passphrase again:
```

Напечатай пароль и нажми Enter. Потом напечатай его ещё раз, такой же.
Пока печатаешь, на экране **ничего не видно**, даже звёздочек. Так и надо.

**Что это значит:** коробка заперта. Без пароля никто не может заглянуть
в неё и достать файлы. Даже имён файлов не видно.

**Открыть запертую коробку:** точно так же, как раньше — посмотреть,
что внутри (раздел 2), вернуть файлы (разделы 3 и 4). Ничего добавлять не надо:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

VBACKUP сам видит, что коробка заперта, и спрашивает:

```
Passphrase for /mnt/usb/ivan.bck:
```

На маленьком компьютере коробка открывается секунду или несколько. Это нарочно:
так подбирать пароль очень долго.

**Пароль — это ключ. Очень важно!**

- Если пароль потерян, коробку **НИКТО** не откроет. Ни ты, ни тот, кто ставил
  тебе компьютер, ни даже автор VBACKUP.
- Бери длинный пароль: пять или больше случайных слов.
- Запиши его на бумажку. Бумажку храни в надёжном месте.

**Сохранять без тебя, по расписанию (cron)? Файловый менеджер?** Там некому печатать пароль.
Тогда положи пароль в файл. Считается только первая строка.
И сделай файл личным — читать его можешь только ты:

```
printf 'my long password words here\n' > /root/backup.key
chmod 600 /root/backup.key
```

Теперь вместо того чтобы печатать, назови файл:

```
vbackup /home/ivan /mnt/usb/ivan.bck /ENCRYPT /KEY_FILE=/root/backup.key
```

Или скажи один раз, и VBACKUP будет каждый раз сам брать этот файл:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

Чтобы запереть новую коробку, `/ENCRYPT` всё равно пиши.

Файловые менеджеры (MC, far2l, Total Commander, Double Commander) спросить
пароль не умеют. Запертую коробку они открывают только так, через `VBACKUP_KEY_FILE`.

**vbkx** (раздел 11) тоже открывает запертую коробку. Дай ему файл через `-k`, или он спросит:

```
vbkx x /mnt/usb/ivan.bck -k /root/backup.key
```

**Полезно знать:**

- Испорченная запертая коробка чинится как раньше (`BLKFIXED`). Пароль для этого не нужен.
- Если кто-то нарочно изменил коробку, VBACKUP это заметит (`BLKFORGED`, раздел 12).

**Внимание:** старая программа VBACKUP (до X01-06) запертую коробку не откроет.
Она просто скажет, что блоки потеряны, и ничего не запишет. Поставь новую версию VBACKUP.

---

## 9. Сохранить целый диск или раздел, как он есть

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
%VBACKUP-I-STARTED, Operation: save, Input: /dev/sdb1, Output: /mnt/usb/sdb1.bck - started
%VBACKUP-I-PHYSSUMM, Device: /dev/sdb1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-SAVESUMM, Files: 1, Bytes: 1507328, Blocks: 29, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.35 - completed
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
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/sdb1.bck, Output: /dev/sdc1 - started
Everything on /dev/sdc1 (134217728 bytes) is to be overwritten with the device saved in /mnt/usb/sdb1.bck.
Type YES to go on:
```

Напечатай большими буквами `YES` и нажми Enter. Любой другой ответ — отказ.

**Что увидишь:**

```
%VBACKUP-I-PHYSLARGER, Device: /dev/sdc1, Bytes: 134217728, Saved: 67108864 - larger: the rest stays as it is, the file system keeps its old size
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-PHYSSUMM, Device: /dev/sdc1, Bytes: 67108864, Data: 1507328 - the rest zeros
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 0.41 - completed
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

## 10. Сохранить всю файловую систему и создать её заново на другом диске

**Чем отличается от раздела 9:** раздел 9 копирует каждый кусочек диска,
а здесь VBACKUP копирует все **файлы** диска и запоминает, какой это был диск.
Новый диск может быть другого размера.

Это тоже делает только **root**.

**Что набрать** (здесь `/mnt/photos` — место, куда подключён диск):

```
vbackup /mnt/photos /mnt/usb/photos.bck /IMAGE
```

**Что увидишь:**

```
%VBACKUP-I-STARTED, Operation: save, Input: /mnt/photos, Output: /mnt/usb/photos.bck - started
%VBACKUP-I-SAVESUMM, Files: 11, Bytes: 760000, Blocks: 16, Volumes: 1 - saved
%VBACKUP-I-COMPLETED, Operation: save, Seconds: 0.09 - completed
```

**Создать диск заново на `/dev/sdc1`.** Это **сотрёт всё** на `/dev/sdc1`!

```
vbackup /mnt/usb/photos.bck /dev/sdc1 /IMAGE /REPLACE
```

**Что увидишь:**

```
%VBACKUP-I-STARTED, Operation: restore, Input: /mnt/usb/photos.bck, Output: /dev/sdc1 - started
%VBACKUP-I-PHYSUUID, Device: /dev/sdc1 - now carries the labels and UUIDs of the device saved: never mount it beside the original
%VBACKUP-I-IMGSUMM, Device: /dev/sdc1, Type: ext4, Files: 11, Bytes: 760000 - file system made, files restored
%VBACKUP-I-COMPLETED, Operation: restore, Seconds: 1.27 - completed
```

**Что это значит:** на `/dev/sdc1` сделан новый диск того же типа (ext4)
с тем же именем, и в него положены все файлы.

Помни:

- для этого нужна программа, которая делает диски нужного типа
  (`mkfs.ext4`, `mkfs.vfat` …). Её ставит тот, кто ставит тебе компьютер;
- так **нельзя** перенести диск, с которого включается компьютер.
  Для этого сохрани весь диск (`/dev/sdb`, не `/dev/sdb1`) способом из раздела 9;
- не подключай старый и новый диск одновременно — они двойники.

---

## 11. Если VBACKUP нет — vbkx

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

## 12. Если что-то пошло не так

Сообщение выглядит так: `%VBACKUP-E-ИМЯ, File: имя - текст`.
Сначала — о чём оно (файл, коробка, диск), потом — что случилось.
Буква после `VBACKUP-` подсказывает, насколько всё серьёзно:

- `I` — просто сообщает. Всё хорошо.
- `W` — предупреждает. Посмотри внимательно.
- `E` или `F` — ошибка. Что-то не сделано.

Последняя строка, `COMPLETED`, говорит, как всё прошло: `completed` — всё хорошо;
`completed with warnings` — посмотри внимательно; `completed with errors` — что-то не сделано.

### FILEEXISTS

**Что ты видишь:**

```
%VBACKUP-W-FILEEXISTS, File: /home/ivan/restored/ivan/letters/anna.txt - already exists, not restored
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

### OPENOUT … errno: 17 (File exists)

**Что ты видишь:**

```
%VBACKUP-E-OPENOUT, File: /mnt/usb/ivan.bck, errno: 17 - cannot be created as output (File exists)
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
%VBACKUP-E-NOTSAVESET, File: /mnt/usb/fake.bck - is not a saveset
```

**Что случилось:** этот файл — не коробка VBACKUP.

**Что сделать:** проверь имя. Посмотри, что лежит на флешке:

```
ls /mnt/usb
```

### OPENIN … errno: 2 (No such file or directory)

**Что ты видишь:**

```
%VBACKUP-E-OPENIN, File: /mnt/usb/nosuch.bck, errno: 2 - cannot be opened as input (No such file or directory)
```

**Что случилось:** такого файла нет (опечатка в имени).

**Что сделать:** проверь имя командой `ls /mnt/usb`.

### NOPARAM

**Что ты видишь:**

```
%VBACKUP-E-NOPARAM, Parameter: output specification - is missing
```

**Что случилось:** ты просил `/LIST`, но файл не коробка, и VBACKUP решил, что ты хочешь
что-то сохранить, а куда — не сказано.

**Что сделать:** проверь имя командой `ls /mnt/usb`.

### IVOP

**Что ты видишь:**

```
%VBACKUP-E-IVOP, cannot tell what to do: the input does not exist - and for a save the output must be named .bck or .sav, or /SAVE_SET given
```

**Что случилось:** того, что ты написал первым, нет. Скорее всего, опечатка.

**Что сделать:** проверь имя:

```
ls /home/ivan
```

Первым стоит коробка? Тогда её имя должно кончаться на `.bck` или `.sav`.
Или добавь `/SAVE_SET`.

### MAXPARM

**Что ты видишь:**

```
%VBACKUP-E-MAXPARM, Parameter: .log - one too many: only an input and an output are taken; a qualifier begins with /
```

**Что случилось:** слишком много слов. Наверное, ты напечатал `.log` вместо `/LOG`.

**Что сделать:** квалификатор всегда начинается с `/`:

```
vbackup /home/ivan /mnt/usb/ivan.bck /LOG
```

### GLUED

**Что ты видишь:**

```
%VBACKUP-I-GLUED, Parameter: box.sav/sav - the qualifiers glued to it are taken as qualifiers
```

**Что случилось:** просто сообщает. `box.sav/sav` понято как `box.sav /SAVE_SET`.

**Что сделать:** ничего. Всё хорошо.

### BLKFIXED

**Что ты видишь:**

```
%VBACKUP-I-BLKFIXED, Block: 3, Volume: 1 - was bad, rebuilt from its group
```

**Что случилось:** кусочек коробки был испорчен, но VBACKUP его сам починил.
**Все файлы целы.**

**Что сделать:** флешка, может быть, начинает портиться.
Скоро сделай новую коробку на другой флешке.

### BLKFORGED

**Что ты видишь:**

```
%VBACKUP-W-BLKFORGED, Block: 3, Volume: 1 - is not what was written: its CRC is right, its authentication fails
```

**Что случилось:** кусочек запертой коробки изменили **нарочно**.
Контрольная сумма у него правильная, но замок говорит: это не то, что записал VBACKUP.

Если сразу за ним идёт `BLKFIXED`, кусочек починен. **Твои файлы целы.**

**Что сделать:** выясни, кто мог писать в коробку.
Храни коробки там, где их никто другой не может изменить.

### BLKLOST и FILDAMAGED

**Что ты видишь:**

```
%VBACKUP-E-BLKLOST, Block: 3, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-BLKLOST, Block: 4, Volume: 1 - is bad and cannot be rebuilt
%VBACKUP-E-FILDAMAGED, File: /home/ivan/restored/ivan/photo1.jpg - is incomplete: its data was lost in bad blocks
```

**Что случилось:** часть коробки испорчена, починить не удалось.
Файлы, названные в `FILDAMAGED`, вернулись не целиком. **Все остальные — целы.**

**Что сделать:** эти файлы возьми из другой коробки, если она есть.
В следующий раз делай две коробки на разных флешках.

### FILLOST

**Что ты видишь:**

```
%VBACKUP-E-FILLOST, File: /home/ivan/restored/ivan/photo10.jpg - not restored: its records were lost in bad blocks
```

**Что случилось:** этот файл пропал вместе с испорченным кусочком. Его вообще нет.

**Что сделать:** возьми его из другой, более старой коробки:

```
vbackup /mnt/usb/old.bck /LIST
```

### UNNAMED

**Что ты видишь:**

```
%VBACKUP-W-UNNAMED, Saveset: /mnt/usb/ivan.bck - blocks were lost and it has no catalog: files missing from the restore cannot all be named
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
%VBACKUP-E-MISSVOL, Volume: 2, Saveset: /mnt/usb/ivan.bck - is missing
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
%VBACKUP-W-NOTRAILER, Saveset: /mnt/usb/ivan.bck - has no trailer: the save did not complete, or its last volume is missing
```

**Что случилось:** сохранение не закончилось (выключили свет, кончилось место)
или нет последнего куска.

**Что сделать:** файлы до места обрыва достать можно — это делается как обычно.
Потом сделай коробку заново.

### NOTINCR

**Что ты видишь:**

```
%VBACKUP-E-NOTINCR, Saveset: /mnt/usb/ivan.bck - it has no catalog: nothing restored with /INCREMENTAL
```

**Что случилось:** для `/INCREMENTAL` нужна целая коробка с «оглавлением». У этой его нет.

**Что сделать:** достань её без `/INCREMENTAL`:

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored
```

### WRONGKEY

**Что ты видишь:**

```
%VBACKUP-E-WRONGKEY, Saveset: /mnt/usb/ivan.bck - the passphrase does not open it
```

**Что случилось:** пароль не тот. Ничего не записано.

**Что сделать:** повтори, медленно. Проверь большие и маленькие буквы (клавиша **Caps Lock**!).
Пароль из файла-ключа? Считается только его первая строка. Посмотри на неё:

```
head -1 /root/backup.key
```

### NOKEY

**Что ты видишь:**

```
%VBACKUP-E-NOKEY, Saveset: /mnt/usb/ivan.bck - needs a passphrase, and there is no terminal to ask it on: give /KEY_FILE=file or VBACKUP_KEY_FILE
```

**Что случилось:** коробка заперта, а спросить пароль негде
(cron, файловый менеджер).

**Что сделать:** дай файл-ключ (раздел 8):

```
vbackup /mnt/usb/ivan.bck /home/ivan/restored /KEY_FILE=/root/backup.key
```

или скажи один раз:

```
export VBACKUP_KEY_FILE=/root/backup.key
```

### KEYFILE

**Что ты видишь:**

```
%VBACKUP-E-KEYFILE, Key file: /root/backup.key - others may read or change it - chmod 600 it
```

**Что случилось:** файл-ключ могут читать другие люди. VBACKUP ему не доверяет.

**Что сделать:** сделай его личным:

```
chmod 600 /root/backup.key
```

То же сообщение бывает, если файл пустой или его первая строка слишком длинная.
Тогда запиши в него пароль заново (раздел 8).

### KEYMATCH

**Что ты видишь:**

```
%VBACKUP-E-KEYMATCH, the two passphrases differ: nothing saved
```

**Что случилось:** два пароля, которые ты напечатал, не совпали. Ничего не сохранено.

**Что сделать:** повтори, медленно. Пока печатаешь, ничего не видно, поэтому печатай внимательно.

### PHYSMOUNTED

**Что ты видишь:**

```
%VBACKUP-E-PHYSMOUNTED, Device: /dev/sdb1 - is mounted read-write on /mnt/photos: unmount it, mount it read-only, or save a snapshot
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
%VBACKUP-E-PHYSHELD, Device: /dev/sdb2 - is in use (swap): free it first, or save what uses it
```

**Что случилось:** диском пользуется сама система (swap, LVM, RAID, шифрование).

**Что сделать:** попроси того, кто ставил компьютер. Сам этот диск не трогай.

### PHYSREPLACE

**Что ты видишь:**

```
%VBACKUP-E-PHYSREPLACE, Device: /dev/sdc1 - everything on it would be overwritten: give /REPLACE to do so
```

**Что случилось:** VBACKUP бережёт диск: без `/REPLACE` он его не стирает.

**Что сделать:** проверь имя диска (`lsblk`). Если точно он — добавь `/REPLACE`.

### PHYSSMALL

**Что ты видишь:**

```
%VBACKUP-E-PHYSSMALL, Device: /dev/sdc1, Bytes: 33554432, Saved: 67108864 - too small, nothing written
```

**Что случилось:** новый диск меньше сохранённого. Всё не поместится.

**Что сделать:** возьми диск побольше. Или верни коробку в файл-образ (раздел 9).

### PHYSABORT

**Что ты видишь:**

```
%VBACKUP-E-PHYSABORT, Device: /dev/sdc1 - not overwritten: the answer was not YES
```

**Что случилось:** ты ответил не `YES`. Ничего не стёрто.

**Что сделать:** если правда хочешь — повтори и напечатай `YES` большими буквами.

### IMGNOTVOL

**Что ты видишь:**

```
%VBACKUP-E-IMGNOTVOL, File: /home/ivan - is neither the mount point of a file system nor a device: /IMAGE saves a whole volume
```

**Что случилось:** `/IMAGE` сохраняет диск целиком, а ты дал обычную папку.

**Что сделать:** дай место, куда подключён диск (например `/mnt/photos`),
или сохрани папку обычным способом (раздел 1).

### IMGNOTMNT

**Что ты видишь:**

```
%VBACKUP-E-IMGNOTMNT, Device: /dev/sdb1 - is not mounted: mount it (read-only is enough) and give the mount point or the device
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
%VBACKUP-E-IMGUNSUPP, Saveset: /mnt/usb/old.bck, Type: minix - VBACKUP does not make such a file system: use /PHYSICAL for it
```

**Что случилось:** такой тип диска VBACKUP делать не умеет.

**Что сделать:** сохрани такой диск способом из раздела 9 (`/PHYSICAL`).
Или достань из коробки только файлы (раздел 3).

### IMGMKFS

**Что ты видишь** (например):

```
%VBACKUP-E-IMGMKFS, Command: mkfs.xfs -f -q -L PHOTOS /dev/sdc1 - failed: the program is not installed
```

**Что случилось:** не получилось сделать новый диск. Чаще всего нет нужной программы.

**Что сделать:** попроси того, кто ставил компьютер, установить её (здесь — `mkfs.xfs`).

### IMGSMALL

**Что ты видишь:**

```
%VBACKUP-E-IMGSMALL, Device: /dev/sdc1, Bytes: 8388608, Needed: 17596518 - too small, nothing written
```

**Что случилось:** файлы не поместятся на этот диск.

**Что сделать:** возьми диск побольше.

---

## 13. Помогите, ничего не понимаю

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
- **Saveset (коробка)** — один большой файл, в котором лежат все твои файлы. Имя кончается на `.bck` или `.sav`.
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
- **Квалификатор** — слово со `/` впереди, например `/LOG`. Говорит VBACKUP, как работать.
- **Пароль (passphrase)** — тайные слова, которые запирают и открывают коробку. Как ключ от двери:
  потеряешь его — и дверь закрыта навсегда.
- **Файл-ключ** — маленький файл, в первой строке которого лежит пароль. Читать его можешь только ты (`chmod 600`).
- **Запертая (зашифрованная) коробка** — коробка, сделанная с `/ENCRYPT`. Без пароля никто не заглянет внутрь,
  даже имён файлов не увидит.
