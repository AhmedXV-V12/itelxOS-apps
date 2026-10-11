# 4. تطبيقات XVR بلغة C

[← الفهرس](README.md) · [English](../en/4-xvr-in-c.md)

التطبيقات المعقدة في هذا المستودع (`edit` و`fm` و`sysmon` و`2048` و`sokoban`
و`mines` و`life` و`sum`) مكتوبة بلغة C ومبنية كملفات تنفيذية XVR بالـ SDK
الموجود في [`sdk/`](../../sdk). وهي الأدوات نفسها التي يُبنى بها itelxOS
برامجه الخاصة.

## ما تحتاجه

نظام لينكس أو **WSL** على ويندوز، فيه `gcc` و`ld` (binutils) و`make`
و`python3`:

```bash
sudo apt install build-essential python3       # Debian, Ubuntu, WSL
```

لا تحتاج أي مكتبة أخرى: البرنامج يستخدم **libitx** (`sdk/system/lib`)، مكتبة
C الصغيرة الخاصة بـ itelx. والأمر `make` لا يعمل في مجلد في مساره مسافات،
لذلك انسخ المستودع في مجلد بلا مسافات.

## المكوّنات

<div dir="rtl">

| الملف | ما هو |
|---|---|
| `sdk/system/lib/include/itx.h` | كل ما يستطيع البرنامج استدعاءه (بالأسفل) |
| `sdk/abi/itx_abi.h` | أرقام استدعاءات النظام وأكواد الأخطاء والهياكل |
| `sdk/system/lib/crt0.S` و`libitx.c` | بداية البرنامج والمكتبة |
| `sdk/system/lib/xvr.ld` | ترتيب الربط: من العنوان 0x400000، الكود R-X، والثوابت R، والبيانات RW |
| `sdk/tools/xvrlink.c` | يحوّل ملف ELF المربوط إلى ملف XVR |
| `sdk/xvr/xvr_format.h` | صيغة XVR |
| `apps/lib/ui.h` | إطارات الشاشة الكاملة والمفاتيح الخام ومولّد أعداد عشوائية (من هذا المستودع) |

</div>

## مرحباً، بلغة C

```c
/* apps/xvr/hello2/hello2.c */
#include <itx.h>

int main(int argc, char **argv) {
    printf("hello from C, pid %d\n", getpid());
    for (int i = 1; i < argc; i++) printf("argument %d: %s\n", i, argv[i]);
    return 0;
}
```

ضعه في المجلد `apps/xvr/hello2/` ثم شغّل `make`: كل مجلد داخل `apps/xvr` هو
تطبيق، يُبنى إلى `build/xvr/hello2.xvr` ويُنسخ إلى `usb/dex/`.

وبدون `make`، الخطوات نفسها يدوياً:

```bash
CFLAGS="-std=gnu11 -O2 -ffreestanding -fno-stack-protector -fno-pic -fno-pie -fno-builtin \
        -fno-tree-loop-distribute-patterns -fno-asynchronous-unwind-tables -Isdk/system/lib/include"
gcc $CFLAGS -c sdk/system/lib/crt0.S -o crt0.o
gcc $CFLAGS -c sdk/system/lib/libitx.c -o libitx.o
gcc $CFLAGS -c hello2.c -o hello2.o
ld -T sdk/system/lib/xvr.ld -nostdlib -static -z noexecstack -o hello2.elf crt0.o libitx.o hello2.o
gcc -O2 -o xvrlink sdk/tools/xvrlink.c
./xvrlink --name hello2 hello2.elf hello2.xvr
```

الأداة `xvrlink` تحتفظ بالمقاطع القابلة للتحميل فقط وتكتب الرأس ومجاميع
التحقق؛ والخياران `--stack` و`--heap` يحددان الأحجام (وإلا فهي 256
كيلوبايت و64 ميجابايت)، والخيار `--update` يصنع برنامج تحديث (بالأسفل).

## ماذا تعطيك libitx

كل شيء موجود في [`itx.h`](../../sdk/system/lib/include/itx.h). الدوال تعيد
رقم خطأ سالباً عند الفشل (لا يوجد `errno`)، و`strerror(r)` تعطي نصه.

<div dir="rtl">

- **الإخراج**: `printf` و`dprintf` و`snprintf` و`puts` و`putchar` و`flush`
  (الإخراج مخزَّن مؤقتاً: نفّذ `flush()` قبل أن تنتظر إدخالاً بنفسك)
- **الذاكرة**: `malloc` و`calloc` و`realloc` و`free`
- **النصوص**: `strlen` و`strcmp` و`strcpy` و`strlcpy` و`strstr` و`strchr`
  و`strdup` و`memcpy` و`memset` و`atoi` و`strtol`، وللـ UTF-8:
  `utf8_decode` و`utf8_encode` و`utf8_len`
- **الملفات**: `open` و`read` و`write` و`close` و`lseek` و`stat` و`readdir`
  و`mkdir` و`remove` و`rename` و`truncate` و`sync` و`read_file` و`write_file`
- **البرامج**: `spawn` و`waitpid` و`kill` و`getpid` و`exit` و`proclist`
- **النظام**: `sysinfo` و`uptime_ms` و`unix_time` و`sleep_ms` و`mounts`
  و`devlist` (أجهزة USB) و`eject` و`xvr_check` و`reboot` و`poweroff`
- **الطرفية**: `tty_mode(TTY_MODE_RAW)` لقراءة المفاتيح واحداً واحداً،
  و`tty_key`، و`tty_info` (عدد الأعمدة والصفوف ولغة لوحة المفاتيح)

</div>

لا توجد أرقام عشرية (floating point) في `printf`، ولا مكتبة C قياسية غير
هذه: احفظ الأرقام كأعداد صحيحة (الأحجام بالكيلوبايت، والمال بالقروش).

## تطبيقات الشاشة الكاملة

طرفية itelx لا تحرّك المؤشر في الشاشة: هي تحتفظ بأسطر من النص. لذلك
التطبيق الذي يملأ الشاشة يرسم إطاراً كاملاً كل مرة: "امسح الشاشة"
(`\x1b[2J`) ثم كل الأسطر، في `write` واحدة. والطرفية لا تعيد رسم إلا الخانات
التي تغيّرت، فلا يحدث وميض. والملف [`apps/lib/ui.h`](../../apps/lib/ui.h) يفعل
هذا لك:

```c
#include <ui.h>

int main(void) {
    int x = 10;
    ui_begin();                                   /* raw keys */
    for (;;) {
        ui_frame();
        ui_printf(C_CYAN "move the star" C_RESET "  (q quits)\n\n");
        ui_printf("%*s*\n", x, "");
        ui_show();
        int k = ui_key();
        if (k == 'q' || k == 27) break;
        if (k == KEY_LEFT && x > 0) x--;
        if (k == KEY_RIGHT && x < 60) x++;
    }
    ui_end();
    return 0;
}
```

الألوان هي ألوان الطرفية الستة: `C_RESET` (الأبيض) و`C_DIM` و`C_YELLOW`
و`C_RED` و`C_GREEN` و`C_CYAN`. وفي وضع المفاتيح الخام تسلّم الطرفيةُ Ctrl+C
للبرنامج كمفتاح عادي بدل أن توقفه؛ لذلك تُنهي `ui_key()` البرنامج عندها،
فيعمل Ctrl+C كما يعمل في كل مكان.

## برامج التحديث

ملف XVR من نوع **update** (`xvrlink --update`) باسم `update.xvr` في أعلى
الفلاشة يعرضه itelx عند توصيل الفلاشة ("are you need update ?"). عند الإجابة
بـ yes يعمل ومعه مسار الفلاشة. والملف [`kit/update.c`](../../kit/update.c) هو
برنامج التحديث الخاص بهذا المستودع: ينسخ مجلد `dex/` من الفلاشة إلى `/dex`،
ويفحص كل `.xvr` بـ `xvr_check` أولاً. التفاصيل في [الفصل 5](5-usb-fat32.md).

## التطبيقات كأمثلة

<div dir="rtl">

| التطبيق | ماذا تتعلم منه |
|---|---|
| [`sum`](../../apps/xvr/sum/sum.c) | قراءة الملفات على أجزاء، و SHA-256 و CRC-32؛ أداة سطر أوامر بسيطة |
| [`sysmon`](../../apps/xvr/sysmon/sysmon.c) | `sysinfo` و`proclist`، وشاشة تتجدد |
| [`2048`](../../apps/xvr/2048/2048.c) | أصغر لعبة شاشة كاملة: إطار، مفاتيح، تراجع، وأعلى نتيجة محفوظة |
| [`sokoban`](../../apps/xvr/sokoban/sokoban.c) | مستويات مكتوبة كنص، وسجل تراجع، وتقدّم محفوظ في `/dex/data` |
| [`mines`](../../apps/xvr/mines/mines.c) | مؤشر على شبكة، والفتح المتسلسل للمربعات |
| [`life`](../../apps/xvr/life/life.c) | حجم الشاشة من `tty_info`، والحركة |
| [`edit`](../../apps/xvr/edit/edit.c) | تحرير الأسطر بالطرفية (الوضع العادي)، ونص في الذاكرة |
| [`fm`](../../apps/xvr/fm/fm.c) | المجلدات، والنسخ بين الأقراص، وتشغيل برامج أخرى بـ `spawn` |

</div>
