# 3. من XVC إلى XVR

[← الفهرس](README.md) · [English](../en/3-xvc-to-xvr.md)

## ما هو ملف XVR

صيغة XVR هي صيغة الملفات التنفيذية في itelxOS (النواة لا تحمّل غيرها).
يتكوّن الملف من رأس (header) حجمه 128 بايت، ثم جدول فيه حتى 16 مقطعاً
(segment)، ثم بايتات المقاطع:

```
+------------------------------+  0
| header: "XVR", version 1,    |
| x86-64, entry point, kind    |
| (program / update), name,    |
| checksums ...                |
+------------------------------+  128
| segment table (32 bytes each)|  أين يوضع كل مقطع في الذاكرة،
+------------------------------+  وهل هو للقراءة / الكتابة / التنفيذ
| the segments' bytes          |
+------------------------------+
```

قبل تشغيل أي شيء تفحص النواة كل شيء: للرأس وللمحتوى مجموع تحقق CRC-32،
والمقاطع يجب أن تبدأ عند حدود صفحة، وألا تتداخل، وألا يكون مقطع قابلاً
للكتابة والتنفيذ معاً، وأن تبقى في منطقة برامج المستخدم. والملف التالف أو
الغريب يُرفض مع ذكر السبب:

```
itelx:/> /media/APPS/pc/damaged.xvr
/media/APPS/pc/damaged.xvr: not a valid XVR executable: payload checksum mismatch (damaged file)
```

## ماذا يصنع xvc2xvr

الأداة `xvc2xvr` تحوّل برنامج XVC إلى ملف تنفيذي XVR. الناتج هو مفسّر XVC
الخاص بـ itelx وفي داخله برنامجك، في مقطع إضافي للقراءة فقط في آخر الملف
(انظر [`sdk/xvcapp`](../../sdk/xvcapp)). عندما يبدأ، يترجم ذلك البرنامج
ويشغّله تماماً كما يفعل `run program.xvc`.

انتبه: البرنامج لا يتحوّل إلى كود آلة، فسرعته هي سرعة ملف `.xvc` نفسه. ما
تكسبه:

<div dir="rtl">

- **ملف واحد** هو برنامج مثل أي برنامج: يعمل باسمه، ويقبله `install`،
  ويحمله تحديث الفلاشة؛
- **فحوصات النواة**: النسخة التي تلفت في الطريق تُرفض ولا تعمل؛
- اسم برنامجك في **رأس** الملف (تعرضه أداة `xvrinfo.py`).

</div>

المفسّر يضيف حوالي 62 كيلوبايت إلى حجم البرنامج.

## الطريقة الأولى: داخل itelx

الأداة `xvc2xvr` واحدة من تطبيقات هذا المستودع (`/dex/xvc2xvr.xvr` بعد
التثبيت):

```
itelx:/> xvc2xvr /dex/guess2.xvc
xvc2xvr: made /dex/guess2.xvr (64279 bytes) from /dex/guess2.xvc
run it by its name: guess2
itelx:/> xvc2xvr /media/STICK/game.xvc /media/STICK/game.xvr
```

الأداة تترجم البرنامج أولاً، والبرنامج الذي فيه خطأ لا يُحوَّل:

```
itelx:/> xvc2xvr broken.xvc
xvc: broken.xvc:2: expected an expression (at ')')
xvc2xvr: broken.xvc was not converted: correct the error above first
```

إذا وُجد `name.xvr` و`name.xvc` معاً في `/dex`، فكتابة الاسم تشغّل `.xvr`.

## الطريقة الثانية: على الكمبيوتر

الأداة [`tools/xvc2xvr.py`](../../tools/xvc2xvr.py) تفعل الشيء نفسه على
ويندوز أو لينكس أو ماك ببايثون 3 (لا شيء آخر يحتاج تثبيتاً). وهي تبني على
الملف `usb/dex/xvc2xvr.xvr` الموجود في هذا المستودع:

```
python tools/xvc2xvr.py myapp.xvc                # يصنع myapp.xvr بجانبه
python tools/xvc2xvr.py myapp.xvc out/game.xvr   # أو في المكان الذي تحدده
```

الكمبيوتر ليس فيه مترجم XVC، لذلك جرّب البرنامج على itelx أولاً
(`run myapp.xvc`) قبل تحويله؛ فملف XVR المصنوع من برنامج فيه خطأ سيعرض الخطأ
عندما يبدأ.

## النظر داخل ملف XVR

```
python tools/xvrinfo.py usb/dex/2048.xvr
python tools/xvrinfo.py --source myapp.xvr       # برنامج XVC الموجود داخله
```

أداة `xvrinfo.py` تجري الفحوصات نفسها التي تجريها النواة، فكلمة "valid"
فيها تعني أن itelx سيقبل الملف. وداخل itelx يجري `install` وتحديث الفلاشة
هذه الفحوصات أيضاً.

## الطريق الآخر إلى XVR: لغة C

الأداة `xvc2xvr` تُبقي برنامجك بلغة XVC. أما البرنامج الذي يحتاج سرعة، أو
يحتاج ما ليس في XVC (رسم شاشة كاملة مع المفاتيح الخام، أو استدعاءات النظام
مثل قائمة العمليات)، فاكتبه بلغة C وابنِه بالـ SDK: [الفصل 4](4-xvr-in-c.md).
