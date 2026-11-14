#!/bin/sh
# =====================================================================
#  سولان — أمر تثبيت الخوادم (لينكس وماك)
#  الاستعمال:  curl -fsSL https://raw.githubusercontent.com/kingxsolo/Solan/main/install-release.sh | sh
#  أو بعد تنزيل المستودع:  sh install-release.sh
#
#  ما يفعله:
#   - يكتشف نظامك ومعماريتك ومدير حزمك
#   - ينزّل حزمة التثبيت الأصلية بأحدث إصدار (أسماء ثابتة من releases/latest)
#       دبيان/أوبونتو/منت   → solan_amd64.deb أو solan_arm64.deb   (apt)
#       فيدورا/ريد هات/سوزي → solan.x86_64.rpm أو solan.aarch64.rpm (dnf/yum/zypper)
#       ماك                 → Solan-macos.pkg                       (installer)
#   - على الأنظمة بلا مدير حزم (ألپاين، آرش، أي توزيعة أخرى):
#       ينزّل حزمة deb ويستخرج الملف التنفيذي الثابت منها ويثبّته يدوياً
#       (التنفيذي musl static — يعمل على أي نواة لينكس بلا أي مكتبات)
#   - يثبّت في /usr/local/bin (أو مكان النظام المعتاد) ويخبرك إن احتاج PATH
#  لا يحتاج أي مكتبات ولا أي أدوات خارجية غير curl أو wget
# =====================================================================
set -eu
REPO="kingxsolo/Solan"
BASE="https://github.com/$REPO/releases/latest/download"

OS="$(uname -s 2>/dev/null || echo unknown)"
ARCH="$(uname -m 2>/dev/null || echo unknown)"

have() { command -v "$1" >/dev/null 2>&1; }

# أداة التنزيل
if have curl; then
  DL="curl -fL --retry 3 -o"
elif have wget; then
  DL="wget -O"
else
  echo "أحتاج curl أو wget للتنزيل."
  exit 1
fi

TMP="$(mktemp -d 2>/dev/null || echo .solan-tmp)"
trap 'rm -rf "$TMP"' EXIT

# صلاحيات المدير
SUDO=""
if [ "$(id -u)" = "0" ]; then
  SUDO=""
elif have sudo; then
  SUDO="sudo"
fi

# ---------------------------------------------------------------------
# ماك: المُثبِّت pkg الرسمي عبر installer
# ---------------------------------------------------------------------
if [ "$OS" = "Darwin" ]; then
  echo "تنزيل حزمة ماك (يونيفرسال: Intel + Apple Silicon)..."
  $DL "$TMP/Solan-macos.pkg" "$BASE/Solan-macos.pkg"
  if [ -n "$SUDO" ] || [ "$(id -u)" = "0" ]; then
    $SUDO installer -pkg "$TMP/Solan-macos.pkg" -target /
    echo "تم التثبيت: /usr/local/bin/solan"
    echo "جرّب: solan --version"
    exit 0
  fi
  # بلا صلاحيات مدير: احفظ الحزمة جانباً ليفتحها المستخدم
  cp "$TMP/Solan-macos.pkg" "$PWD/Solan-macos.pkg"
  echo "لا أملك صلاحيات مدير لتشغيل installer."
  echo "حُفظت الحزمة في: $PWD/Solan-macos.pkg — افتحها بنقرة مزدوجة (زر يمين ← فتح إن منع Gatekeeper)."
  exit 0
fi

if [ "$OS" != "Linux" ]; then
  echo "منصة غير مدعومة لأمر التثبيت الجاهز: $OS"
  echo "استعمل البناء من المصدر:  git clone https://github.com/$REPO && cd Solan && sh install.sh"
  exit 1
fi

# ---------------------------------------------------------------------
# لينكس: تحديد أسماء الحزم حسب المعمارية
# ---------------------------------------------------------------------
case "$ARCH" in
  x86_64)  DEB="solan_amd64.deb"    ; RPM="solan.x86_64.rpm"  ;;
  aarch64) DEB="solan_arm64.deb"    ; RPM="solan.aarch64.rpm" ;;
  *)
    echo "لا يوجد إصدار جاهز للمعمارية $ARCH."
    echo "استعمل البناء من المصدر (دقائق قليلة):"
    echo "  git clone https://github.com/$REPO && cd Solan && sh install.sh"
    exit 1
    ;;
esac

# ---------------------------------------------------------------------
# دبيان/أوبونتو/منت — حزمة deb عبر apt
# ---------------------------------------------------------------------
if have apt-get; then
  echo "تنزيل $DEB ..."
  $DL "$TMP/solan.deb" "$BASE/$DEB"
  if [ -n "$SUDO" ] || [ "$(id -u)" = "0" ]; then
    $SUDO apt-get install -y "$TMP/solan.deb"
  else
    echo "لا أملك صلاحيات مدير لتثبيت الحزمة على النظام."
    echo "الحزمة منزّلة في: $TMP/solan.deb — لكن مجلد مؤقت سيُحذف عند الخروج."
    echo "أعد التشغيل بصلاحيات مدير:  sudo sh install-release.sh"
    exit 1
  fi
  echo "تم التثبيت عبر apt."
  echo "جرّب: solan --version"
  exit 0
fi

# ---------------------------------------------------------------------
# فيدورا/ريد هات/سنتوس — حزمة rpm عبر dnf أو yum
# ---------------------------------------------------------------------
if have dnf || have yum || have zypper; then
  echo "تنزيل $RPM ..."
  $DL "$TMP/solan.rpm" "$BASE/$RPM"
  if have dnf; then
    $SUDO dnf install -y "$TMP/solan.rpm"
  elif have yum; then
    $SUDO yum install -y "$TMP/solan.rpm"
  else
    $SUDO zypper --non-interactive install "$TMP/solan.rpm"
  fi
  echo "تم التثبيت."
  echo "جرّب: solan --version"
  exit 0
fi

# ---------------------------------------------------------------------
# بلا مدير حزم مدعوم (ألپاين، آرش، أي توزيعة أخرى):
# استخراج الملف التنفيذي الثابت من حزمة deb وتثبيته يدوياً
# ---------------------------------------------------------------------
echo "لا يوجد apt/dnf/zypper — سأستخرج الملف التنفيذي الثابت من حزمة deb."
echo "تنزيل $DEB ..."
$DL "$TMP/solan.deb" "$BASE/$DEB"

BIN=""
if have dpkg-deb; then
  mkdir -p "$TMP/x" && dpkg-deb -x "$TMP/solan.deb" "$TMP/x"
  BIN="$TMP/x/usr/bin/solan"
elif have ar && have tar; then
  # حزمة deb أرشيف ar بداخله data.tar.* — نفكه بأدوات النظام نفسها
  mkdir -p "$TMP/ar" && (cd "$TMP/ar" && ar x "$TMP/solan.deb")
  DATA="$(find "$TMP/ar" -name 'data.tar*' | head -n1)"
  [ -n "$DATA" ] || { echo "لم أجد data.tar داخل الحزمة"; exit 1; }
  mkdir -p "$TMP/x" && tar xf "$DATA" -C "$TMP/x"
  BIN="$TMP/x/usr/bin/solan"
fi

if [ -n "$BIN" ] && [ -f "$BIN" ]; then
  if [ -d /usr/local/bin ] && { [ "$(id -u)" = "0" ] || [ -w /usr/local/bin ]; }; then
    DEST="/usr/local/bin"
  elif [ -n "${PREFIX:-}" ] && [ -d "$PREFIX" ] && [ -w "$PREFIX" ]; then
    DEST="$PREFIX/bin"
  else
    DEST="$HOME/.local/bin"
  fi
  mkdir -p "$DEST"
  if [ -n "$SUDO" ] && [ "$DEST" = "/usr/local/bin" ]; then
    $SUDO install -m 755 "$BIN" "$DEST/solan"
  else
    install -m 755 "$BIN" "$DEST/solan"
  fi
  echo "تم التثبيت: $DEST/solan"
  case ":$PATH:" in
    *":$DEST:"*) ;;
    *) echo "تنبيه: أضف $DEST إلى PATH — مثلاً:"
       echo "  echo 'export PATH=\"$DEST:\$PATH\"' >> ~/.profile && . ~/.profile"
       ;;
  esac
  echo "جرّب: solan --version"
  exit 0
fi

echo "تعذّر الاستخراج (لا dpkg-deb ولا ar). خياران:"
echo "  1) نزّل حزمة مناسبة يدوياً من: https://github.com/$REPO/releases/latest"
echo "  2) البناء من المصدر (دقائق قليلة):"
echo "       git clone https://github.com/$REPO && cd Solan && sh install.sh"
exit 1
