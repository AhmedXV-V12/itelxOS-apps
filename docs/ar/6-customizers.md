# 6. تطبيقات التخصيص (Customizer apps)

[← الفهرس](README.md) · [English](../en/6-customizers.md)

**تطبيق التخصيص** ملف `.xvr` واحد يحوّل itelx إلى نظام متخصص في عمل واحد.
كل ما يثبّته موجود داخله: البرنامج الذي يظهر بدل الطرفية، والأدوات التي
معه، ولاثنين منها نواة itelx فيها تعريفات (درايفرات) أكثر. يُثبَّت تطبيق
تخصيص واحد في كل مرة، و**recustomize** يعيد itelx كما كان بعد تثبيته
مباشرة.

<div dir="rtl">

| التطبيق | يصبح itelx | يظهر بدل الطرفية | النواة |
|---|---|---|---|
| `noter` | دفتر يوميات | Noter بملء الشاشة | itelx 1.4 |
| `school_mode` | مكتب طالب | School mode بملء الشاشة | itelx 1.4 |
| `work_mode` | مكان عمل | Work mode بملء الشاشة | itelx 1.4 |
| `file_saver` | خزنة لملفات من أي نوع | طرفيته الخاصة (`vault:/>`) | itelx 1.4 net: إيثرنت |
| `data_place` | مركز بيانات: SQL ومستندات ومفتاح-قيمة ومتجهات | طرفيته الخاصة (`data:/>`)، شاشة سوداء وكتابة بيضاء | itelx 1.4 data: إيثرنت |
| `recustomize` | itelx كما ثُبّت | طرفية itelx | الأصلية |

</div>

تجدها في [`customizers/release`](../../customizers/release). ولكل واحد
نسختان:

<div dir="rtl">

- `NAME.xvr` هو الذي يثبّت.
- `NAME.txt` للقراءة فقط: ما هو، وكل ملف يضعه على itelx بحجمه وبصمة
  SHA-256 الخاصة به، والكود المصدري لبرنامج التثبيت.

</div>

والكود المصدري لكل شيء في [`customizers/`](../../customizers): التطبيقات،
وبرنامج التثبيت، وتعديلات النواة، والأدوات التي تحزمها. تطبيقات التخصيص
تحتاج **itelxOS 1.4**: على إصدار آخر تقول ذلك ولا تغيّر شيئاً.

## 1. ثبّت واحداً

انسخ ملف `.xvr` إلى فلاشة FAT32 ([الفصل 5](5-usb-fat32.md)) ووصّلها بـ itelx.
في الأمثلة اسمها `/media/CZ`.

### Noter و School mode و Work mode

```
itelx:/> /media/CZ/noter.xvr
```

يعرض برنامج التثبيت ما هو وكل ملف سيكتبه، ثم يسأل
`Install it? (yes/no)`. اكتب `yes`. وعندما ينتهي يسأل
`Start it now? (yes/no)`. ومن الآن يشغّل itelx برنامج التطبيق بدل الطرفية،
وبعد كل إعادة تشغيل أيضاً.

وهناك طريقتان أخريان تفعلان الشيء نفسه: `install /media/CZ/noter.xvr` ثم
`noter`، أو نسخ الملف إلى أعلى الفلاشة باسم `update.xvr` (فيسأل itelx
`are you need update ?` عند توصيل الفلاشة).

### File saver و data_place: معهما نواة

النواة موجودة في قسم الإقلاع الخاص بالنظام، و itelx 1.4 لا يسمح لأي برنامج
بالكتابة هناك إلا أثناء تحديث وافقتَ عليه من فلاشة USB. لذلك يُثبَّت هذان
من الفلاشة:

<div dir="rtl">

1. شغّله من الطرفية: `/media/CZ/file_saver.xvr`. سيقول إن معه نواته الخاصة،
   وينسخ نفسه إلى الفلاشة باسم `update.xvr`، ويخبرك بالخطوة التالية. وإن
   كان على الفلاشة `update.xvr` من قبل فإنه يُحفظ جانباً ويعود بعد ذلك.
2. اكتب `eject /media/CZ`، وانزع الفلاشة ثم وصّلها مرة أخرى. سيسأل itelx
   `are you need update ?`: اكتب `yes`، ثم `yes` لسؤال `Install it?`.
3. يكتب الملفات والنواة، وينظّف الفلاشة، ويسأل `Restart now? (yes/no)`.
   النواة الجديدة تعمل من التشغيل التالي.

</div>

### ما يتحقق منه برنامج التثبيت

<div dir="rtl">

- كل ملف بداخله يُفحص (CRC-32) قبل أن يُكتب أي شيء.
- كل ملف يُكتب باسم `NAME.new`، ثم يُقرأ ويُقارن، وبعدها فقط يأخذ مكان
  القديم. انقطاع الكهرباء يترك الملف القديم أو الجديد، ولا يترك نصف ملف أبداً.
- يحتاج مساحة فارغة على قرص النظام قدر حجم ملفاته مرتين.
- ما ثُبّت يُكتب في `/system/customizer.txt`. ولا يقبل تطبيق تخصيص آخر أن
  يُثبَّت ما دام هناك واحد (شغّل `recustomize` أولاً)، أما تثبيت التطبيق
  نفسه مرة أخرى فيصلحه.

</div>

## 2. العودة إلى itelx كما ثُبّت: recustomize

كل تطبيق تخصيص يثبّت `/system/recustomize.xvr`. وتشغّله هكذا:

<div dir="rtl">

- في Noter أو School mode أو Work mode: **Esc**، ثم *System*، ثم *Restore the system*؛
- في File saver أو data_place: اكتب `recustomize`؛
- أو من فلاشة: الملف `recustomize.xvr` من `customizers/release`.

</div>

يعرض ما سيفعله ويطلب منك كتابة `YES` (بحروف كبيرة). ثم:

<div dir="rtl">

- ملفات النظام التي ليست الأصلية تعود. ملفات itelx 1.4 محفوظة داخله،
  مأخوذة من مثبّت 1.4، فيعمل حتى لو تغيّرت تلك الملفات أو حُذفت؛
- النواة الأصلية تعود. وإن لم تكن النواة الحالية هي الأصلية فهذا يتم عن
  طريق الفلاشة كما في التثبيت أعلاه؛
- **كل ملف ومجلد ليس في النظام الأصلي يُحذف نهائياً**: ما كُتب مع تطبيق
  التخصيص، وما كُتب قبله أيضاً؛
- تبقى `/boot/cmdline` و `/system/hostname` و `/system/owner`: كُتبت لهذا
  الكمبيوتر عند تثبيت itelx.

</div>

خذ نسخة احتياطية قبلها: `backup` في File saver، و `dbbackup` في data_place،
و *Export* من قائمة Noter.

## 3. Noter

<p align="center"><img src="../../screenshots/customizers/noter.png" alt="Noter" width="760"></p>

دفتر يوميات. شاشاته (المفاتيح من 1 إلى 5، و Tab للتالية):

<div dir="rtl">

- **Timeline**: تدويناتك حسب الشهر، ولكل واحدة مزاجها وعدد كلماتها
  ووسومها. وسؤال اليوم فوقها.
- **Calendar**: الأيام التي كتبت فيها.
- **On this day**: ما كتبته قبل أسبوع وشهر وسنوات.
- **Insights**: أيام الكتابة المتتالية، والأمزجة، والوسوم، والكلمات.
- **Prompts**: أسئلة تكتب عنها.

</div>

لكل تدوينة مزاج، ووسوم `#tags`، ودفتر، ويمكن أن تكون مفضلة. والقوالب تبدأ
التدوينة لك: تأمل يومي، وامتنان، ومراجعة المساء، وغيرها. والمفتاح `/`
للبحث. ويمكن قفل Noter برمز سري، ومن القائمة تُصدَّر اليوميات إلى فلاشة.
يعمل بالعربية والإنجليزية؛ Alt+Shift يبدّل لغة لوحة المفاتيح. والبيانات في
`/home/noter`.

## 4. School mode

<p align="center"><img src="../../screenshots/customizers/school.png" alt="School mode" width="760"></p>

مكتب طالب، فيه سبع شاشات (المفاتيح من 1 إلى 7):

<div dir="rtl">

| الشاشة | ما فيها |
|---|---|
| Today | حصص اليوم، والواجبات المطلوبة، والامتحانات القادمة، ووقت المذاكرة هذا الأسبوع |
| Homework | يوم التسليم، والأولوية، والتقدم؛ والمسافة (Space) تجعله منتهياً |
| Timetable | حصص كل يوم من الأسبوع |
| Timers | مؤقت مذاكرة لكل مادة: بطوله الخاص، أو بومودورو، أو ساعة إيقاف |
| Exams | عدّ تنازلي وموضوعات كل امتحان |
| Diary | صفحة لكل يوم دراسي |
| Stats | وقت المذاكرة لكل مادة مقارنة بهدفها الأسبوعي، وآخر ثمانية أسابيع، والدرجات |

</div>

ومن القائمة (Esc) تفتح الأدوات أيضاً: الآلة الحاسبة، ومحوّل الوحدات، وجدول
الضرب، والتدريب على الكتابة، والاختبار (تطبيقات XVC من هذا المستودع،
تُثبَّت في `/dex`). والبيانات في `/home/school`.

## 5. Work mode

<p align="center"><img src="../../screenshots/customizers/work.png" alt="Work mode" width="760"></p>

مكان عمل لمستقل أو فريق صغير، فيه ثماني شاشات:

<div dir="rtl">

| الشاشة | ما فيها |
|---|---|
| Dashboard | اجتماعات اليوم، والمطلوب، وساعات الأسبوع، والمشروعات المتأخرة أو التي تجاوزت ميزانيتها، والصفقات، والفواتير غير المدفوعة |
| Tasks | قائمة أو لوحة (backlog، to do، doing، review، done)؛ أولويات، ومواعيد تسليم، وتقديرات، وقوائم تحقق، ومهام تنتظر غيرك |
| Projects | الحالة، والعميل، والموعد النهائي، والميزانية بالساعات، وسعر الساعة، والتقدم |
| Time | مؤقت على مهمة أو مشروع يستمر بعد إعادة التشغيل؛ وجدول ساعات الأسبوع، مدفوعة أو لا |
| Calendar | اجتماعات بجدول أعمال ومحضر؛ وبنود العمل فيها تصبح مهام |
| Clients | الأشخاص والشركات، ومسار الصفقات |
| Notes | مستندات، حسب المشروع |
| Reports | الساعات والمال والمهام لأسبوع أو شهر أو سنة |

</div>

ومن القائمة: **فواتير** تُصنع من الوقت المدفوع لعميل، و**المصروفات**. والأدوات:
الآلة الحاسبة، والمحوّل، و `fm`، و `edit`، و `sum`. والبيانات في `/home/work`.

## 6. File saver

<p align="center"><img src="../../screenshots/customizers/vault.png" alt="منظّم الخزنة في File saver" width="760"></p>

ملفات من أي نوع تُحفظ في `/vault`، مرتبة في مجلدات حسب نوعها: Documents
و Pictures و Music و Videos و Archives و Apps و Code و Data و Private
و Other. والملفات الجديدة تصل إلى Inbox.

<div dir="rtl">

- كل ملف مفهرس ببصمة SHA-256 الخاصة به. والأمر `check` يجد أي ملف تغيّر أو
  اختفى.
- الملف الذي يُستبدل تبقى نسخته السابقة.
- الملفات المحذوفة تذهب إلى سلة المهملات أولاً.
- يمكن **قفل** الملفات بكلمة مرور الخزنة. التشفير ChaCha20 مع HMAC-SHA-256،
  والمفتاح يُشتق من كلمة المرور بـ PBKDF2. كلمة المرور المنسية لا يمكن
  استرجاعها، ولا الملفات المقفلة بها.

</div>

في طرفيته كل أوامر itelx، وهذه أيضاً:

<div dir="rtl">

| الأمر | ما يفعله |
|---|---|
| `vault` | المنظّم بملء الشاشة: فتح، ونسخ إلى الخزنة ومنها، ونقل، وإعادة تسمية، وقفل، وبحث، وفحص، وسلة مهملات، ونسخ احتياطي |
| `store PATH [FOLDER]` | ملف أو مجلد إلى الخزنة، مرتباً حسب نوعه |
| `organize` | ملفات Inbox إلى مجلداتها |
| `find TEXT` | ملفات الخزنة التي في اسمها TEXT |
| `space` | المساحة التي يأخذها كل مجلد |
| `check`، `reindex` | مقارنة كل ملف ببصمة SHA-256 الخاصة به؛ صنع فهرس جديد |
| `password` | تعيين كلمة مرور الخزنة أو تغييرها |
| `lock PATH`، `unlock PATH` | تشفير ملف (أو ملفات مجلد)، وفك تشفيره |
| `trash [PATH]` | نقل إلى سلة المهملات، أو عرضها؛ `trash restore NAME`، `trash empty` |
| `versions FILE` | النسخ السابقة من ملف |
| `backup [STICK]`، `restore [STICK]` | الخزنة إلى فلاشة (ما تغيّر فقط)، واسترجاعها منها |
| `share [FOLDER] [PORT]` | مجلد إلى الشبكة، للقراءة فقط: المتصفح على كمبيوتر آخر يفتح `http://ADDRESS:PORT/` |

</div>

وأوامر الشبكة في [القسم 8](#8-الشبكة).

## 7. data_place

<p align="center"><img src="../../screenshots/customizers/data_place.png" alt="data_place" width="760"></p>

قواعد بيانات من أربعة أنواع، على شاشة سوداء بكتابة بيضاء. ملفاتها في
`/data`.

<div dir="rtl">

| الأداة | ما هي |
|---|---|
| `sql` | قواعد بيانات SQL: **SQLite 3.54.1** بالمعاملات (transactions)، والفهارس، و JSON، والبحث في النصوص (FTS5)، ودوال الرياضيات والنوافذ؛ واستيراد CSV وتصديره |
| `doc` | قواعد مستندات بأسلوب MongoDB: مستندات JSON، وفلاتر (`$gt` و `$in` و `$or` ...)، وتعديلات (`$set` و `$inc` و `$push` ...) |
| `kv` | قواعد مفتاح-قيمة بأسلوب Redis: نصوص، وعدّادات، و hashes، وقوائم، ومجموعات، وانتهاء صلاحية؛ و `kv serve` يتكلم بروتوكول Redis |
| `vec` | مجموعات متجهات للذكاء الاصطناعي: البحث عن الأقرب بـ cosine أو L2 أو dot product، وفلاتر على البيانات الوصفية، ومجموعات نصوص |
| `dbscan` | نوع ملف البيانات، من محتواه: حوالي 40 نوعاً من قواعد البيانات وملفات الذكاء الاصطناعي |
| `dbstat` | كل قواعد البيانات في نظرة واحدة |
| `dbserve` | كلها كواجهة HTTP API بـ JSON، محمية برمز (token) |
| `dbbackup`، `dbrestore` | `/data` إلى فلاشة واسترجاعها، وكل ملف يُفحص بـ SHA-256 |

</div>

SQLite هنا هو SQLite الحقيقي، مبنياً لـ itelx (ملكية عامة، من sqlite.org).
أما kv و doc و vec فكُتبت لـ data_place: doc يحفظ مستنداته في SQLite، و kv
يحتفظ بسجل يُضاف إليه فقط، و vec له ملفاته الخاصة.

```
data:/> sql main "create table city (name text, people int)"
data:/> sql main "insert into city values ('Cairo', 10000000), ('Aswan', 1500000)"
data:/> sql main "select name from city where people > 2000000"
data:/> sql main                            the SQL shell: .help, .tables, .mode, .import ...

data:/> doc main insert people {"name":"Mona","age":25}
data:/> doc main find people {"age":{"$gt":20}}

data:/> kv main SET greeting hello
data:/> kv main HSET user:1 name Ahmed

data:/> vec create notes 128 cosine text
data:/> vec add-text notes n1 "the kernel of an operating system"
data:/> vec search-text notes "operating system kernel" k 1
```

كل أداة تُشغَّل بدون أمر تفتح واجهتها الخاصة، وفيها قائمة أوامرها:
`.help` في sql، و `help` في doc و vec، و `HELP` في kv.

**من كمبيوتر آخر.** في الحالتين يجب أن يصل الكمبيوتر إلى عنوان itelx
(الأمر `net`).

<div dir="rtl">

- `kv serve main 6379 PASSWORD &` يقدّم قاعدة مفتاح-قيمة ببروتوكول Redis،
  فيتصل بها `redis-cli -h ADDRESS -a PASSWORD` ومكتبات Redis. وبدون كلمة
  مرور تُصنع واحدة وتُعرض.
- `dbserve 8086 TOKEN &` يقدّم كل شيء بـ JSON:

</div>

```bash
curl -H "Authorization: Bearer TOKEN" http://ADDRESS:8086/
curl -H "Authorization: Bearer TOKEN" -d "SELECT * FROM city" http://ADDRESS:8086/sql/main
curl -H "Authorization: Bearer TOKEN" -d '{"filter": {"name": "Mona"}}' http://ADDRESS:8086/doc/main/people/find
curl -H "Authorization: Bearer TOKEN" -d '["GET", "greeting"]' http://ADDRESS:8086/kv/main
curl -H "Authorization: Bearer TOKEN" -d '{"text": "a kernel", "k": 3}' http://ADDRESS:8086/vec/notes/search
```

الطلب بدون الرمز يأخذ `401`. و dbserve يجيب طلباً واحداً في كل مرة. وكلاهما
يستخدم HTTP و TCP عاديين بدون تشفير، فاستخدمهما على شبكة تثق بها.

**متجهات النصوص.** مجموعة النصوص تحوّل الكلمات إلى متجهات على itelx نفسه
بطريقة feature hashing (العربية بدون تشكيل، وصور الألف والتاء المربوطة
والألف المقصورة تُعامل كحرف واحد). لذلك `search-text` يجد النصوص التي تشترك
في الكلمات، لا النصوص التي تتشابه في المعنى. وللبحث بالمعنى اصنع المتجهات
بنموذج ذكاء اصطناعي على الكمبيوتر واستوردها: `vec import NAME FILE` يقبل
`.csv` أو `.jsonl`.

## 8. الشبكة

مع File saver و data_place نواة itelx فيها إيثرنت:

<div dir="rtl">

| كارت الشبكة | التعريف |
|---|---|
| Intel PRO/1000: 82540EM و 82545EM و 82544GC | e1000 |
| Intel 82574L | e1000e |
| Realtek RTL8139 | rtl8139 |
| virtio-net (الأجهزة الافتراضية) | virtio-net |

</div>

العنوان يأتي من DHCP بمجرد توصيل الكابل. وفي النواة IPv4 مع ARP و ICMP
و UDP و DNS و TCP. وهذه الأوامر في الطرفيتين:

<div dir="rtl">

| الأمر | ما يفعله |
|---|---|
| `net` | الكروت، والعنوان، والراوتر، و DNS |
| `net dhcp` | طلب عنوان من DHCP من جديد |
| `net static IP/BITS ROUTER DNS` | تعيين العنوان يدوياً |
| `ping HOST [N]` | هل يرد كمبيوتر، وبأي سرعة |
| `dns NAME` | عنوان اسم |
| `fetch URL [FILE]` | تنزيل ملف عبر `http://` |

</div>

**ما ليس فيها:**

<div dir="rtl">

- **الواي فاي.** شرائح الواي فاي تحتاج firmware من الشركة المصنعة، وتعريف
  الواي فاي لا يمكن اختباره على جهاز افتراضي، فلم يُضف تعريف لم يُرَ يعمل.
- الكروت التي ليست في الجدول، مثل Realtek 8111/8168 أو Intel I219.
- IPv6.
- `https://`.

</div>

والأمر `net` يقول إذا لم يكن لأي كارت تعريف.

## 9. بناؤها

على لينكس أو WSL، بما يحتاجه [الفصل 4](4-xvr-in-c.md):

```bash
make customizers
```

هذا يبني كل تطبيقات التخصيص و recustomize في `customizers/release` (ملفات
`.xvr` و `.txt` و `SHA256SUMS`). وما يثبّته كل تطبيق مكتوب في
`customizers/NAME/payload.txt`، وبرامجه هي مجلدات `customizers/NAME/apps`.

النواتان في `customizers/kernel/out`. وتُبنيان من الكود المصدري لـ itelx 1.4
بإضافة ملفات الشبكة من `customizers/kernel/src` وتطبيق
`customizers/kernel/itelx-1.4-net.patch`:

```bash
bash customizers/kernel/build-kernel.sh /path/to/itelxOS64bit
```

والاختبار `tests/e2e_customizers.py` يجرّبها كلها على itelxOS 1.4 حقيقي في
QEMU. فلكل تطبيق تخصيص يثبّته، ويستخدمه بمفاتيح تُكتب على لوحة المفاتيح،
ثم يعيد النظام بـ recustomize. ويختبر الشبكة أيضاً مع كل كارت في الجدول
أعلاه: DHCP، و ping، وتنزيل ملف حجمه 3 ميجابايت يجب أن تطابق بصمة SHA-256
الخاصة به نسخة الكمبيوتر.
