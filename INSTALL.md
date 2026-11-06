# سولان — دليل التثبيت الكامل

سولان لغة برمجة عربية. ملف تنفيذي واحد بلا أي متطلبات: **SQLite مضمّنة داخله**، ولا يحتاج مكتبات خارجية أو أطر تشغيل أو صلاحيات مدير (في أغلب الطرق).

> كل الإصدارات الجاهزة في صفحة [Releases](https://github.com/kingxsolo/Solan/releases) — أحدث إصدار دائماً على الرابط المثبّت: <https://github.com/kingxsolo/Solan/releases/latest>

## أي ملف أنزّل؟

| منصتك | أنزّل | الطريقة |
|---|---|---|
| ويندوز 64-بت | `Solan-*-Setup-x64.exe` | [§ ويندوز — المُثبِّت](#1-ويندوز) |
| ويندوز 64-بت (بلا تثبيت) | `Solan-*-windows-x64.zip` | [§ ويندوز — محمول](#ب-نسخة-محمولة-بلا-تثبيت) |
| ويندوز 32-بت | `Solan-*-windows-x86.zip` | [§ ويندوز — محمول](#ب-نسخة-محمولة-بلا-تثبيت) |
| لينكس (أي توزيعة) | `Solan-*-linux-x64.tar.gz` | [§ لينكس](#2-لينكس) |
| أوبونتو / دبيان / منت | `solan_*_amd64.deb` | [§ لينكس — حزمة deb](#ب-حزمة-أصلية-deb--rpm) |
| فيدورا / ريد هات / أوبن سوزي | `solan-*.x86_64.rpm` | [§ لينكس — حزمة rpm](#ب-حزمة-أصلية-deb--rpm) |
| ماك (Intel أو Apple Silicon) | `Solan-*-macos.pkg` | [§ ماك](#3-ماك-macos) |
| ماك (بلا تثبيت) | `Solan-*-macos-universal.tar.gz` | [§ ماك — محمول](#ب-الحزمة-المحمولة) |
| ترمكس (أندرويد) | — (بناء من المصدر) | [§ ترمكس](#4-ترمكس-أندرويد) |

---

## 1) ويندوز

### أ) المُثبِّت `Setup.exe` (الأسهل)

1. أنزّل `Solan-1.1.0-Setup-x64.exe` من صفحة [Releases](https://github.com/kingxsolo/Solan/releases/latest).
2. شغّله — الواجهة عربية بالكامل، وتختار ما تريد:
   - إضافة سولان إلى `PATH` (مفعّل افتراضياً).
   - ربط ملفات `.سو` بسولان (فتحها من المستكشف يشغّلها).
   - أيقونة على سطح المكتب وقائمة ابدأ.
3. انتهى. افتح نافذة طرفية **جديدة** واكتب:

```bat
solan --version
```

> إن ظهر تحذير SmartScreen («Windows حمى جهازك») فالملف غير موقّع رقمياً: اضغط **معلومات أكثر ← تشغيل على أي حال**.

**إزالة التثبيت**: الإعدادات ← التطبيقات ← التطبيقات المثبتة ← سولان ← إزالة (أو `Uninstall سولان` من قائمة ابدأ).

### ب) نسخة محمولة (بلا تثبيت)

1. أنزّل `Solan-1.1.0-windows-x64.zip` (أو `-x86.zip` لويندوز 32-بت).
2. فك الضغط في أي مجلد — مثلاً `C:\solan`.
3. شغّل `solan.exe` من الطرفية، أو أضف المجلد إلى `PATH` يدوياً:

```bat
:: للمستخدم الحالي فقط (بلا صلاحيات مدير)
setx PATH "%PATH%;C:\solan"
```

### ج) البناء من المصدر (بلا MSYS2 ولا WSL)

```powershell
git clone https://github.com/kingxsolo/Solan
cd Solan
powershell -ExecutionPolicy Bypass -File install.ps1
```

السكربت ينزّل مترجم MinGW **محمولاً** (~264MB مرة واحدة، داخل مجلد المشروع لا النظام)، يبني `solan.exe`، ويثبّته في `%LOCALAPPDATA%\solan\bin` ويضيفه إلى `PATH`. لا يحتاج صلاحيات مدير.

### ملاحظات ويندوز

- **`https` و`wss`**: يحتاج OpenSSL 3 وقت التشغيل فقط عند استعماله: `winget install ShiningLight.OpenSSL.Light` ثم ضع `libssl-3-x64.dll` بجوار `solan.exe`. كل ما عدا ذلك يعمل بلا أي إضافات.
- **مسارات الحزم الطويلة**: إن ظهرت مشاكل مع مسارات المكتبات، فعّل «Long Paths» أو اجعل `SOLAN_HOME` قصيراً (مثل `C:\so`).
- **32-بت**: نسخة `-x86.zip` مفسَّرة كامل الوظائف (الـJIT للـ64-بت فقط).

---

## 2) لينكس

### أ) أمر واحد — تنزيل أحدث إصدار الجاهز

```sh
curl -fsSL https://raw.githubusercontent.com/kingxsolo/Solan/main/install-release.sh | sh
```

أو بعد تنزيل المستودع: `sh install-release.sh`. يثبّت في `/usr/local/bin` إن أمكن، وإلا `~/.local/bin` (بلا صلاحيات مدير)، ويخبرك إن احتاج `PATH`.

الملف الثابت (musl static): يعمل على **أي توزيعة** — أوبونتو، دبيان، فيدورا، آرش، ألپاين — بلا أي مكتبات.

### ب) حزمة أصلية (deb / rpm)

```sh
# دبيان / أوبونتو / منت
sudo apt install ./solan_1.1.0_amd64.deb

# فيدورا / ريد هات / سنتوس
sudo dnf install ./solan-1.1.0-1.x86_64.rpm

# أوبن سوزي
sudo zypper install ./solan-1.1.0-1.x86_64.rpm
```

### ج) الحزمة الثابتة يدوياً

```sh
tar xzf Solan-1.1.0-linux-x64.tar.gz
sudo install -m755 Solan-1.1.0-linux-x64/solan /usr/local/bin/

# أو بلا صلاحيات مدير:
mkdir -p ~/.local/bin && install -m755 Solan-1.1.0-linux-x64/solan ~/.local/bin/
echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.profile && . ~/.profile
```

### د) البناء من المصدر

```sh
git clone https://github.com/kingxsolo/Solan && cd Solan
sh install.sh
```

يكتشف السكربت توزيعتك ومدير حزمها، ويثبّت مترجم C إن نقص، ويبني مع SQLite مضمّنة، ويثبّت في `/usr/local/bin` أو `~/.local/bin`. خيارات مفيدة: `--prefix DIR`، `--build-only`، `--no-sqlite`، `--uninstall`.

### ملاحظات لينكس

- **`https` و`wss`**: تحتاج `libssl` وقت التشغيل عند استعمالها — موجودة افتراضياً في كل التوزيعات تقريباً (`apt install libssl3` / `dnf install openssl-libs` إن نقصت).
- **ARM64 / 32-بت**: لا توجد حزم جاهزة — البناء من المصدر يعمل مباشرة (`sh install.sh`)، والتنفيذ مفسَّر كامل الوظائف.

---

## 3) ماك (macOS)

الملف **يونيفرسال**: يعمل على Intel وApple Silicon (M1/M2/M3/M4) معاً.

### أ) المُثبِّت `pkg`

1. أنزّل `Solan-1.1.0-macos.pkg` من [Releases](https://github.com/kingxsolo/Solan/releases/latest).
2. شغّله وأكمل الخطوات — يثبّت `solan` في `/usr/local/bin` (سيطلب كلمة سر المدير).
3. **إن منع Gatekeeper الفتح** (الملف غير موقّع بشهادة Apple المدفوعة)، أيٌّ من هذه يكفي:
   - **زر يمين على الملف ← فتح ← فتح** (مرة واحدة تكفي).
   - أو: إعدادات النظام ← الخصوصية والأمان ← «تم منع استخدام…» ← **فتح على أي حال**.
   - أو من الطرفية قبل الفتح: `xattr -d com.apple.quarantine Solan-1.1.0-macos.pkg`

### ب) الحزمة المحمولة

```sh
tar xzf Solan-1.1.0-macos-universal.tar.gz
sudo install -m755 Solan-1.1.0-macos/solan /usr/local/bin/

# أو بلا صلاحيات مدير:
mkdir -p ~/.local/bin && install -m755 Solan-1.1.0-macos/solan ~/.local/bin/
```

إن اشتكى الماك من الملف المنزّل: `xattr -d com.apple.quarantine solan` (أو على المجلد كاملاً).

### ج) البناء من المصدر

```sh
git clone https://github.com/kingxsolo/Solan && cd Solan
sh install.sh
```

يتطلب أدوات سطر الأوامر فقط (`xcode-select --install` إن نقص `clang`).

### ملاحظات ماك

- **`https` و`wss`**: يحتاج OpenSSL 3 وقت التشغيل عند استعماله: `brew install openssl@3`.
- التنفيذ **مفسَّر كامل الوظائف** (الـJIT حالياً لويندوز ولينكس x86-64).

---

## 4) ترمكس (أندرويد)

لا يوجد ملف جاهز (المعمارية ARM) — البناء من المصدر داخل ترمكس يستغرق دقائق قليلة:

```sh
pkg update
pkg install -y clang git
git clone https://github.com/kingxsolo/Solan
cd Solan
sh install.sh
```

- يكتشف السكربت بيئة ترمكس تلقائياً ويثبّت في `$PREFIX/bin` (موجود في `PATH` أصلاً).
- **بلا JIT** — المفسّر كامل الوظائف، وSQLite تُضمَّن إن توفرت ملفاتها.
- لـ `https`: `pkg install openssl`.
- جرّب: `solan --version` ثم `solan -e 'اطبع("هلا من الأندرويد")'`.

---

## 5) جدول الدعم: البيئات والمعماريات

| البيئة | المعمارية | ملف جاهز؟ | JIT | المفسّر |
|---|---|---|---|---|
| ويندوز 10/11 | x64 (64-بت) | Setup.exe + zip | ✓ مفعّل | — |
| ويندوز | x86 (32-بت) | zip | ✗ | ✓ كامل |
| لينكس | x86-64 | tar.gz ثابت + deb + rpm | ✓ مفعّل | — |
| لينكس | ARM64 / 32-بت | من المصدر | ✗ | ✓ كامل |
| ماك | Intel + Apple Silicon | pkg + tar.gz | ✗ | ✓ كامل |
| ترمكس (أندرويد) | ARM | من المصدر | ✗ | ✓ كامل |
| BSD | x86-64 | من المصدر | ✗ | ✓ كامل |

- **نفس الشجرة المصدرية للجميع** — نفس اللغة والمكتبات والبايت-كود؛ الفرق الوحيد سرعة الحلقات (JIT مقابل مفسَّر).
- SQLite مضمّنة في كل الملفات الجاهزة.

---

## 6) بعد التثبيت — التحقق وأول برنامج

```sh
solan --version        # سولان 1.1.0
solan                  # الطرفية التفاعلية
solan -e 'اطبع("هلا يا عالم")'
```

أول ملف: احفظ `هلا.سو`:

```solan
اطبع("هلا يا عالم")
لكل ي في مدى(1, 5) { اطبع("العد: " + ي) }
```

وشغّله: `solan هلا.سو`

---

## 7) إزالة التثبيت

| البيئة | الطريقة |
|---|---|
| ويندوز (Setup.exe) | الإعدادات ← التطبيقات ← سولان ← إزالة |
| ويندوز (محمول) | احذف المجلد واحذف مدخله من `PATH` |
| ويندوز (install.ps1) | احذف `%LOCALAPPDATA%\solan` واحذف مدخله من `PATH` |
| لينكس (deb) | `sudo apt remove solan` |
| لينكس (rpm) | `sudo dnf remove solan` |
| لينكس/ماك/ترمكس (يدوي أو install.sh) | `sh install.sh --uninstall` أو احذف ملف `solan` من مجلد التثبيت |

---

## 8) حل المشكلات الشائعة

| المشكلة | الحل |
|---|---|
| `solan: command not found` / «ليس أمراً داخلياً» | افتح طرفية **جديدة** بعد التثبيت، أو أضف مجلد التثبيت إلى `PATH` يدوياً |
| ويندوز: SmartScreen يحظر Setup.exe | «معلومات أكثر» ← «تشغيل على أي حال» (الملف غير موقّع رقمياً) |
| ماك: «لا يمكن فتحه لأنه من مطوّر غير موثوق» | زر يمين ← فتح، أو `xattr -d com.apple.quarantine` (تفصيل في § ماك) |
| `https` لا يعمل | ثبّت OpenSSL 3 حسب منصتك (تفصيل في قسم كل منصة)، أو استعمل `SOLAN_LIBSSL` لمسار المكتبة صراحة |
| لينكس: «لا يمكن تنفيذ الملف» | تأكد من الصلاحيات: `chmod +x solan` — وإن كان الجهاز ARM فهذا الملف x64، استعمل البناء من المصدر |
| ترمكس: فشل `git clone` | تأكد من `pkg install -y git` ومن الاتصال؛ أو نزّل المستودع ZIP من GitHub |
