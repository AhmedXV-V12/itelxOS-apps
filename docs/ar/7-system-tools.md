# 7. أدوات النظام: formater و recovery_tester

[← الفهرس](README.md) · [English](../en/7-system-tools.md)

برنامجان XVR مكتوبان بلغة C لنظام **itelxOS 2.0**. يعملان مع شاشة
الاسترداد الحمراء في itelx 2.0 ونظام النسخة الاحتياطية فيه، لذلك يحتاجان
itelx 2.0: نواة itelx 1.x ترفض تشغيلهما ("needs a newer itelx").

<div dir="rtl">

| الأداة | ماذا تفعل |
|---|---|
| `formater` | تمسح قرص النظام كله ما عدا النواة وملفات الإقلاع |
| `recovery_tester` | تصنع عطلًا مقصودًا، وتتأكد أن شاشة الاسترداد الحمراء تصلحه، ثم تعيد النظام كما كان |

</div>

تجدهما في [`system-tools/`](../../system-tools) وبصمتهما SHA-256 في
`SHA256SUMS`. ليستا في عدّة الفلاشة (`usb/`)، لأن العدّة تُثبَّت على كل
إصدارات itelx، وهاتان الأداتان مكانهما itelx 2.0 فقط. انسخهما إلى فلاشة
FAT32 وشغّلهما منها:

```
itelx:/> /media/STICK/formater.xvr
itelx:/> /media/STICK/recovery_tester.xvr
```

كلتاهما تسأل **y/n** قبل أن تغيّر أي شيء. ليس في itelx حسابات مستخدمين:
كل برنامج يعمل بصلاحيات المالك (root)، فهذا السؤال هو ما يحمي النظام.

## قبل أن تبدأ: شاشة الاسترداد الحمراء

عندما لا يستطيع itelx 2.0 أن يبدأ، يعرض مدير الإقلاع شاشة الاسترداد
الحمراء بدل النظام. فيها خياران:

<div dir="rtl">

- **Use backup**: يحفظ ملفاتك في `/users/recovered-<date>`، ثم يضع نظام
  النسخة الاحتياطية (itelx صغير محفوظ على قسم الإقلاع) على القرص: تصبح
  `/kernel` و`/system` و`/boot` هي التي في نظام النسخة الاحتياطية. ويعود
  itelxOS الكامل بـ `update.xvr`.
- **Use manual**: طرفية مؤقتة في الذاكرة على الشاشة الحمراء؛ ويُفتح قرص
  النظام في `/old` بالأمر `disk.open`.

</div>

`/users` هو المجلد الذي يتركه "Use backup" في مكانه، ولهذا تحفظ
recovery_tester ملفاتها فيه.

## formater

```
itelx:/> /media/STICK/formater.xvr
formater: this wipes the system disk.

Removed, with everything inside:
  /system
  /dex
  /users
  /notes.txt
Kept: /kernel  /boot  /media (USB sticks)  /ram (memory)

Without /system the system cannot start: the next start shows the red
recovery screen, where "Use backup" puts the backup system back.
The backup system is on the boot partition (\recovery\recovery.img).

Wipe the system disk? (y/n)
```

<div dir="rtl">

- يُمسح كل ما في أعلى قرص النظام بكل محتوياته: `/system` و`/dex` و`/users`
  وأي ملف أو مجلد آخر.
- ما يبقى: `/kernel` (النواة ووحداتها) و`/boot` (ملفات الإقلاع). ولا تقترب
  الأداة من `/media` أبدًا (الفلاشات والأقراص الأخرى)، و`/ram` ذاكرة وليست
  القرص، وأي قرص آخر مركّب في أعلى الشجرة يُتخطّى.
- تقول أولًا هل نظام النسخة الاحتياطية موجود على قسم الإقلاع. إن لم يكن
  موجودًا فلن يعيد itelxOS إلا تثبيت جديد (مثبّت ODE).
- في النهاية تسأل `Restart now? (y/n)`. في التشغيل التالي يجد itelx أنه لا
  يستطيع العمل (لا يوجد مدير الخدمات)، فيعلن أنه فشل ويعيد التشغيل إلى شاشة
  الاسترداد الحمراء، وهناك يضع **Use backup** نظام النسخة الاحتياطية على
  القرص.
- المجلد الذي يعمل فيه برنامج لا يمكن حذفه (والأداة تقول أيّه). اذهب إلى
  `/` في الطرفية وشغّل formater مرة أخرى.
- في الطرفية المؤقتة لشاشة الاسترداد لا تفعل شيئًا، لأن قرص النظام ليس
  الجذر هناك.

</div>

## recovery_tester

```
recovery_tester           start a test, or check the one under way
recovery_tester cancel    undo the fault now
```

### 1. بداية الاختبار

```
itelx:/> /media/STICK/recovery_tester.xvr
recovery_tester: a test of the red recovery screen

1. The good copy of /system/xvinit.xvr (... bytes) goes to /users/recovery_tester/xvinit.xvr.good
2. /system/xvinit.xvr is damaged on purpose: the system cannot start with it
3. The next start shows the red recovery screen; repair the system there
   (Use backup keeps /users and puts the backup system on the disk)
4. Run recovery_tester again on the repaired system: it puts the good copy back,
   or writes why the repair failed to /users/recovery_tester/report-ar.txt and report-en.txt

Start the test? (y/n)
```

قبل أن تغيّر الأداة أي شيء تتأكد من وجود قسم الإقلاع وحالة التشغيل ونظام
النسخة الاحتياطية، ومن أن `/system/xvinit.xvr` سليم. ثم:

<div dir="rtl">

- تُحفظ النسخة السليمة من `/system/xvinit.xvr` (مدير الخدمات، أول برنامج
  يشغّله itelx) في `/users/recovery_tester/xvinit.xvr.good`، ثم تُقرأ من
  جديد وتُقارن (الحجم و CRC-32).
- يسجّل الملف `/users/recovery_tester/state.txt` بيانات الاختبار: الملف،
  وحجمه و CRC-32 الخاص به، ومكان العطل، ووقت البداية، والنواة.
- العطل: تُقلب 64 بايت في منتصف `/system/xvinit.xvr`، فلا يطابق الملف بصمته،
  وترفض النواة تشغيله كما ترفض ملفًا تلف على قرص حقيقي.

</div>

`Restart now? (y/n)`: يكمل الاختبار في التشغيل التالي. وحتى ذلك الحين
يعيد `recovery_tester cancel` النسخة السليمة ولا يترك شيئًا.

### 2. شاشة الاسترداد الحمراء

في التشغيل التالي يقول itelx إن مدير الخدمات `/system/xvinit.xvr` لم يبدأ
(`itelx cannot run`) ويعيد التشغيل، فتظهر شاشة الاسترداد الحمراء. أصلح
النظام منها: **Use backup**، أو **Use manual** والإصلاح باليد. في الطرفية
المؤقتة لـ Use manual افتح قرص النظام بـ `disk.open`، وعندها يعمل
`recovery_tester cancel` هناك أيضًا على الملفات التي في `/old`.

### 3. الفحص

على النظام بعد إصلاحه شغّل recovery_tester مرة أخرى:

<div dir="rtl">

- **إن نجح الإصلاح** (عاد `/system/xvinit.xvr` برنامجًا سليمًا، وبدأ النظام،
  والنسخة السليمة كما هي): تضع الأداة النسخة السليمة مكان الملف الذي أُصلح
  فورًا، وتقرؤها من جديد، وتحذف مجلدها من `/users`:

  ```
  recovery_tester: PASSED - the red recovery screen repaired the system, and the
  original file is back.
  ```

  بعد Use backup يعمل الكمبيوتر بنظام النسخة الاحتياطية: ثبّت `update.xvr`
  عليه ليعود itelxOS الكامل.
- **إن فشل الإصلاح**: تقول الأداة السبب، وتكتب تقريرين:
  `/users/recovery_tester/report-ar.txt` بالعربية و
  `/users/recovery_tester/report-en.txt` بالإنجليزية. في كل منهما الأسباب،
  وما وُجد (الملف، والنسخة السليمة، وحالة التشغيل، وهل نظام النسخة
  الاحتياطية على القرص، والمجلدات التي حفظها Use backup، والنواة)،
  والسجلات: `state.txt` وآخر سجل النواة. ويبقى الاختبار مسجّلًا، فيستطيع
  `recovery_tester cancel` أن يعيد النسخة السليمة.

</div>

الأسباب التي تبحث عنها: الملف التالف لم يعد على القرص، أو ما زال تالفًا، أو
النسخة السليمة مفقودة أو تالفة، أو لم يكتمل أي تشغيل منذ بداية الاختبار، أو
تعذّر وضع النسخة السليمة مكان الملف الذي أُصلح.

## كيف اختُبرتا

يشغّل `tests/e2e_system_tools.py` الأداتين على itelxOS 2.0 حقيقي في QEMU
بنوعي الإقلاع UEFI و BIOS: من فلاشة FAT32، مع كتابة y و n على لوحة المفاتيح،
مرورًا بإعادة التشغيل، وشاشة الاسترداد الحمراء (يُفحص لونها على الشاشة)،
وUse backup، والفحص بعده: PASSED، وFAILED مع التقريرين عندما حُذفت النسخة
السليمة أثناء الاختبار.

```
python3 tests/e2e_system_tools.py --image /path/to/itelxOS.img --fw uefi,bios
```

[← الفهرس](README.md)
