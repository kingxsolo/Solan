#!/bin/sh
# =====================================================================
#  سولان — تنزيل أحدث إصدار جاهز وتثبيته (لينكس وماك)
#  الاستعمال:  sh install-release.sh
#  يثبّت في $PREFIX/bin (ترمكس) أو /usr/local/bin أو ~/.local/bin
#  لا يحتاج صلاحيات مدير ولا أي مكتبات — الملف الثابت مكتفٍ بذاته
# =====================================================================
set -eu
REPO="kingxsolo/Solan"

OS="$(uname -s 2>/dev/null || echo unknown)"
ARCH="$(uname -m 2>/dev/null || echo unknown)"

# نمط اسم الملف الجاهز (الاسم الكامل يتضمن رقم الإصدار فنجيبه من الواجهة)
case "$OS" in
  Linux)
    case "$ARCH" in
      x86_64) PAT="linux-x64" ;;
      *)
        echo "لا يوجد إصدار جاهز للمعمارية $ARCH."
        echo "في ترمكس أو ARM استعمل البناء من المصدر (دقائق قليلة):"
        echo "  pkg install -y clang git 2>/dev/null"
        echo "  git clone https://github.com/$REPO && cd Solan && sh install.sh"
        exit 1
        ;;
    esac
    ;;
  Darwin) PAT="macos-universal" ;;
  *)
    echo "منصة غير مدعومة للتنزيل الجاهز: $OS"
    echo "استعمل البناء من المصدر: sh install.sh"
    exit 1
    ;;
esac

have() { command -v "$1" >/dev/null 2>&1; }
if have curl; then
  FETCH="curl -fsSL --retry 3"
  DL="curl -fL --retry 3 -o"
elif have wget; then
  FETCH="wget -qO-"
  DL="wget -O"
else
  echo "أحتاج curl أو wget للتنزيل (أو استعمل: sh install.sh للبناء من المصدر)"
  exit 1
fi

echo "البحث عن أحدث إصدار..."
JSON="$($FETCH "https://api.github.com/repos/$REPO/releases/latest")" || {
  echo "تعذّر الوصول إلى GitHub (شبكة أو حد الاستخدام؟). نزّل يدوياً من:"
  echo "  https://github.com/$REPO/releases"
  exit 1
}
URL="$(printf '%s\n' "$JSON" | grep '"browser_download_url"' | grep "$PAT" | head -n1 | sed 's/.*"\(http[^"]*\)".*/\1/')"
[ -n "$URL" ] || {
  echo "لم أجد ملفاً جاهزاً ($PAT) في أحدث إصدار. نزّل يدوياً من:"
  echo "  https://github.com/$REPO/releases"
  exit 1
}

TMP="$(mktemp -d 2>/dev/null || echo .solan-tmp)"
trap 'rm -rf "$TMP"' EXIT
echo "تنزيل $(basename "$URL") ..."
$DL "$TMP/حزمة.tar.gz" "$URL"
mkdir -p "$TMP/فك"
tar xzf "$TMP/حزمة.tar.gz" -C "$TMP/فك"

# مكان التثبيت: بيئة ترمكس ($PREFIX مضبوط تلقائياً) ثم /usr/local ثم ~/.local
if [ -n "${PREFIX:-}" ] && [ -d "$PREFIX" ] && [ -w "$PREFIX" ]; then
  DEST="$PREFIX/bin"
elif [ -d /usr/local/bin ] && [ -w /usr/local/bin ]; then
  DEST="/usr/local/bin"
else
  DEST="$HOME/.local/bin"
fi
mkdir -p "$DEST"
BIN="$(find "$TMP/فك" -type f -name solan | head -n1)"
[ -n "$BIN" ] || { echo "لم أجد الملف التنفيذي في الحزمة"; exit 1; }
install -m 755 "$BIN" "$DEST/solan"

echo "تم التثبيت: $DEST/solan"
case ":$PATH:" in
  *":$DEST:"*) ;;
  *) echo "تنبيه: أضف $DEST إلى PATH — مثلاً:"
     echo "  echo 'export PATH=\"$DEST:\$PATH\"' >> ~/.profile && . ~/.profile"
     ;;
esac
echo "جرّب: solan --version"
