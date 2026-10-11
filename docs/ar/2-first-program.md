# 2. برنامجك الأول

[← الفهرس](README.md) · [English](../en/2-first-program.md)

## أين تكتبه

**على الكمبيوتر** (ويندوز أو لينكس أو ماك): بأي محرر نصوص (Notepad أو
VS Code...). احفظ الملف بترميز **UTF-8** باسم ينتهي بـ `.xvc`، مثلاً
`hello.xvc`، ثم انسخه إلى فلاشة ([الفصل 5](5-usb-fat32.md)).

في Notepad اختر الترميز *UTF-8* وليس *UTF-8 with BOM*: الـ BOM ثلاث بايتات
مخفية في أول الملف، والأمر `run` في itelx يقرؤها كجزء من أول كلمة (فيظهر
الخطأ `undefined name 'print'` في السطر 1). أما `xvc2xvr` فيحذفها عند
التحويل.

**داخل itelx نفسه**: بتطبيق `edit` الموجود في هذا المستودع

```
edit /dex/hello.xvc
```

اكتب `a print("hello")` ثم Enter، ثم `w` (حفظ) ثم `q` (خروج). وإن لم يكن
`edit` مثبّتاً فأمر الطرفية `write` يكفي: `write /dex/hello.xvc`، اكتب
الأسطر، وأنهِ بسطر فيه نقطة `.` وحدها.

## شغّله

```
itelx:/> run /dex/hello.xvc
hello
itelx:/> hello
hello
```

البرنامج الموجود في `/dex` يعمل باسمه وحده. والبرنامج الموجود على فلاشة
يعمل من مكانه: `run /media/STICK/hello.xvc`، والأمر
`install /media/STICK/hello.xvc` ينسخه إلى `/dex`.

## برنامج حقيقي خطوة بخطوة

لعبة صغيرة "خمّن الرقم". اكتبها في ملف `guess2.xvc`:

```
# guess2.xvc - guess the number from 1 to 100
let secret = random(100) + 1
let tries = 0
print("I picked a number from 1 to 100.")

while true {
    let line = input("your guess: ")
    if line == nil { break }            # Ctrl+D: the end of the input
    let g = int(line)
    if g == nil {
        print("that is not a number")
        continue
    }
    tries += 1
    if g < secret { print("higher") }
    elif g > secret { print("lower") }
    else {
        print("right, in " + str(tries) + " tries")
        break
    }
}
```

ماذا يفعل كل جزء:

<div dir="rtl">

- الدالة `random(100)` تعطي من 0 إلى 99، فـ `+ 1` تجعلها من 1 إلى 100.
- الدالة `input("your guess: ")` تعرض النص وتنتظر سطراً من المستخدم.
- الدالة `int(line)` تحوّل النص إلى عدد، أو تعطي `nil` إذا لم يكن عدداً:
  تحقق منها دائماً قبل استخدامها.
- الأمر `continue` يعود إلى أول الحلقة، و`break` يخرج منها.
- الدالة `str(tries)` تحوّل العدد إلى نص لنجمعه بـ `+`.

</div>

## عندما يكون هناك خطأ

اصنع خطأً عن قصد: احذف القوس الأخير `)` من
`print("right, in " + str(tries) + " tries")`. عند التشغيل يظهر:

```
xvc: guess2.xvc:19: expected ')' after the arguments (at 'break')
[exit 2]
```

لم يعمل أي شيء، لأن الملف يُترجم قبل أن يبدأ، ورقم السطر يدلّك على مكان
الخطأ. أما الخطأ الذي لا يظهر إلا أثناء التشغيل (مثل فهرس غير موجود في
قائمة) فيبدو هكذا:

```
xvc: guess2.xvc:7: runtime error: list index 3 out of range (length 2)
```

ويتوقف البرنامج عند ذلك السطر.

## المعاملات والألوان والمفاتيح

```
# hi.xvc - hi <name>
if len(args) < 2 {
    print("use: hi <name>")
    exit(1)
}
print(color("green") + "hi " + args[1] + "!" + color("reset"))
print("press a key")
print("you pressed: " + key())
```

```
itelx:/> hi Sara
hi Sara!
press a key
you pressed: x
```

## حفظ البيانات

البرامج تحفظ ملفاتها في `/dex/data`:

```
let FILE = "/dex/data/visits.txt"
if not exists("/dex/data") { makedir("/dex/data") }
let n = 0
let old = readfile(FILE)
if old != nil { n = int(trim(old)) }
n += 1
writefile(FILE, str(n))
print("this program ran " + str(n) + " time(s)")
```

## الخطوة التالية

<div dir="rtl">

- اقرأ التطبيقات في [`apps/xvc`](../../apps/xvc): كل واحد منها قصير ويبدأ
  بتعليق يشرح طريقة استخدامه.
- اصنع ملفاً تنفيذياً XVR من برنامجك: [الفصل 3](3-xvc-to-xvr.md).
- لتجربة البرامج على الكمبيوتر أولاً: الأمر `make host-xvc` يبني مفسّر XVC
  للينكس أو WSL أو ماك (`build/host/xvc hello.xvc`). هو المفسّر نفسه، لكن
  المسارات مسارات الكمبيوتر (لا يوجد `/dex`).

</div>
