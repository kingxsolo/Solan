#!/bin/sh
set -u
cd "$(dirname "$0")"

usage() {
  cat <<'USAGE_END'
الاستعمال: sh install.sh [خيارات]

  --prefix DIR     مكان التثبيت (الافتراضي /usr/local أو ~/.local عند الحاجة)
  --no-deps        لا يحاول تثبيت المترجم تلقائيًا
  --no-sqlite      عطّل SQLite المضمّن إن كانت ملفاته موجودة
  --build-only     ابنِ ./solan فقط بلا تثبيت
  --uninstall      احذف solan من مكان التثبيت
  --target NAME    فرض المنصة: linux | macos | bsd | windows | android
  --dry-run        اعرض الأوامر بلا تنفيذ

متغيرات البيئة: CC وCFLAGS وCPPFLAGS وLDFLAGS
USAGE_END
}

PREFIX_ARG=""; DEPS=1; SQLITE=1; DRY=0; BUILD_ONLY=0; UNINSTALL=0; TARGET_ARG=""
while [ $# -gt 0 ]; do
  case "$1" in
    --prefix) [ $# -ge 2 ] || { echo "الخيار --prefix يحتاج قيمة"; exit 2; }; PREFIX_ARG="$2"; shift 2 ;;
    --target) [ $# -ge 2 ] || { echo "الخيار --target يحتاج قيمة"; exit 2; }; TARGET_ARG="$2"; shift 2 ;;
    --no-deps) DEPS=0; shift ;;
    --no-sqlite) SQLITE=0; shift ;;
    --build-only) BUILD_ONLY=1; shift ;;
    --uninstall) UNINSTALL=1; shift ;;
    --dry-run) DRY=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) echo "خيار غير معروف: $1"; exit 2 ;;
  esac
done
case "$TARGET_ARG" in
  ""|linux|macos|bsd|windows|android) ;;
  *) echo "منصة غير معروفة: $TARGET_ARG"; exit 2 ;;
esac
[ "$BUILD_ONLY" = 1 ] && [ "$UNINSTALL" = 1 ] && { echo "لا يمكن الجمع بين --build-only و--uninstall"; exit 2; }
[ "$UNINSTALL" = 1 ] && DEPS=0

run() { echo "+ $*"; [ "$DRY" = 1 ] || "$@"; }
have() { command -v "$1" >/dev/null 2>&1; }

OS="$(uname -s 2>/dev/null || echo unknown)"
ARCH="$(uname -m 2>/dev/null || echo unknown)"
KIND=unknown
PM=""
case "$OS" in
  Darwin) KIND=macos ;;
  FreeBSD|OpenBSD|NetBSD|DragonFly) KIND=bsd ;;
  MINGW*|MSYS*|CYGWIN*) KIND=windows ;;
  Linux)
    KIND=linux
    if [ -r /etc/os-release ]; then . /etc/os-release; fi
    case "${ID:-} ${ID_LIKE:-}" in
      *debian*|*ubuntu*|*raspbian*) PM=apt ;;
      *fedora*|*rhel*|*centos*|*rocky*|*almalinux*) PM=dnf ;;
      *arch*|*manjaro*) PM=pacman ;;
      *alpine*) PM=apk ;;
      *suse*|*opensuse*|*sles*) PM=zypper ;;
    esac
    ;;
esac
[ -n "$TARGET_ARG" ] && KIND="$TARGET_ARG"
if [ -z "$PM" ] && [ "$KIND" = linux ]; then
  for c in apt-get dnf pacman apk zypper; do
    if have "$c"; then PM="$c"; break; fi
  done
  [ "$PM" = apt-get ] && PM=apt
fi

echo "المنصة: $KIND ($OS $ARCH)${PM:+، مدير الحزم: $PM}"
[ "$KIND" = windows ] && echo "للويندوز الأصلي استعمل install.ps1 أو بيئة MSYS2."

SUDO=""
if [ "$(id -u 2>/dev/null || echo 1)" != 0 ]; then
  have sudo && SUDO=sudo
  [ -n "$SUDO" ] || { have doas && SUDO=doas; }
fi

compiler_ok() {
  [ -n "${CC:-}" ] && have "${CC%% *}" && return 0
  for c in cc gcc clang; do have "$c" && return 0; done
  return 1
}

install_linux_compiler() {
  for method in "$PM" apt dnf pacman apk zypper; do
    case "$method" in
      apt)
        have apt-get || continue
        echo "محاولة تثبيت المترجم عبر apt."
        run $SUDO apt-get update && run $SUDO apt-get install -y build-essential && return 0
        ;;
      dnf)
        have dnf || continue
        echo "محاولة تثبيت المترجم عبر dnf."
        run $SUDO dnf install -y gcc glibc-devel && return 0
        ;;
      pacman)
        have pacman || continue
        echo "محاولة تثبيت المترجم عبر pacman."
        run $SUDO pacman -Sy --needed --noconfirm base-devel && return 0
        ;;
      apk)
        have apk || continue
        echo "محاولة تثبيت المترجم عبر apk."
        run $SUDO apk add --no-cache build-base && return 0
        ;;
      zypper)
        have zypper || continue
        echo "محاولة تثبيت المترجم عبر zypper."
        run $SUDO zypper --non-interactive install gcc glibc-devel && return 0
        ;;
    esac
  done
  return 1
}

install_compiler() {
  if [ "$KIND" = linux ]; then
    install_linux_compiler && return 0
    echo "فشلت محاولات مديري الحزم المتاحين على Linux."
    return 1
  fi
  case "$KIND:$PM" in
    macos:*)
      if have xcode-select; then run xcode-select --install || true; fi
      if ! compiler_ok && have brew; then run brew install llvm || true; fi
      ;;
    bsd:*)
      echo "المترجم عادةً متوفر مع أدوات النظام؛ ثبّته من مدير حزم النظام إذا لم يكن موجودًا."
      return 1
      ;;
    windows:*)
      echo "ثبّت مترجم MSYS2 ثم أعد التشغيل، أو استعمل install.ps1."
      return 1
      ;;
    *)
      echo "لم أتعرف على مدير الحزم. جرّبت الكشف التلقائي ولم أغيّر النظام."
      return 1
      ;;
  esac
  compiler_ok
}

if [ "$DEPS" = 1 ] && ! compiler_ok; then
  echo "لم أجد مترجم C؛ سأحاول تثبيته بالطريقة المناسبة للبيئة."
  install_compiler || echo "تعذر تثبيت المترجم تلقائيًا. ثبّت gcc أو clang ثم أعد التشغيل."
fi

if [ "$UNINSTALL" = 0 ]; then
  if [ -z "${CC:-}" ]; then
    for c in cc gcc clang; do have "$c" && { CC="$c"; break; }; done
  fi
  CC="${CC:-cc}"
  have "${CC%% *}" || [ "$DRY" = 1 ] || { echo "لا يوجد مترجم C ($CC)"; exit 1; }
fi

EXE=solan
[ "$KIND" = windows ] && EXE=solan.exe

if [ "$BUILD_ONLY" = 0 ]; then
  if [ -n "$PREFIX_ARG" ]; then PREFIX="$PREFIX_ARG"
  elif [ -n "${PREFIX:-}" ]; then :
  else
    PREFIX=/usr/local
    if [ ! -w "$PREFIX" ] && [ -z "$SUDO" ] && [ "$(id -u 2>/dev/null || echo 1)" != 0 ]; then
      PREFIX="$HOME/.local"
      echo "لا صلاحية على /usr/local — سأستعمل $PREFIX"
    fi
  fi
  export PREFIX
  INSTALL_SUDO=""
  [ -w "$PREFIX" ] || [ "$(id -u 2>/dev/null || echo 1)" = 0 ] || INSTALL_SUDO="$SUDO"
fi

if [ "$UNINSTALL" = 1 ]; then
  run $INSTALL_SUDO rm -f "$PREFIX/bin/solan" "$PREFIX/bin/solan.exe"
  echo "تم حذف solan من $PREFIX/bin"
  exit 0
fi

LINK_EXPORT="-rdynamic"; LINK_LIBS="-lm -ldl"
case "$KIND" in
  macos) LINK_EXPORT="-Wl,-export_dynamic"; LINK_LIBS="-lm" ;;
  windows) LINK_EXPORT=""; LINK_LIBS="-lm -lws2_32 -lshell32 -ladvapi32" ;;
  bsd) LINK_EXPORT="-rdynamic"; LINK_LIBS="-lm" ;;
esac
BUILD_CFLAGS="-O2 -Wno-format-truncation ${CPPFLAGS:-} ${CFLAGS:-}"

EMBED_DEF=""; EMBED_OBJ=""
if [ "$SQLITE" = 1 ] && [ -f third_party/sqlite/sqlite3.c ]; then
  mkdir -p third_party/sqlite
  echo "SQLite: مضمَّن في الملف التنفيذي"
  run $CC -O2 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION ${CPPFLAGS:-} ${CFLAGS:-} -c -o third_party/sqlite/sqlite3.o third_party/sqlite/sqlite3.c
  EMBED_DEF="-DSOLAN_SQLITE_EMBED"; EMBED_OBJ="third_party/sqlite/sqlite3.o"
else
  echo "SQLite: اختياري؛ سيُحمّل من مكتبة النظام وقت التشغيل عند توفرها."
fi

run rm -f "$EXE"
run $CC $BUILD_CFLAGS $EMBED_DEF -o "$EXE" solan.c $EMBED_OBJ $LINK_EXPORT ${LDFLAGS:-} $LINK_LIBS
[ "$DRY" = 1 ] || [ -f "$EXE" ] || { echo "فشل البناء: $EXE غير موجود"; exit 1; }

if [ "$BUILD_ONLY" = 1 ]; then
  echo "تم البناء: ./$EXE"
  exit 0
fi

run $INSTALL_SUDO mkdir -p "$PREFIX/bin"
run $INSTALL_SUDO cp "$EXE" "$PREFIX/bin/$EXE"
run $INSTALL_SUDO chmod 755 "$PREFIX/bin/$EXE"
case ":$PATH:" in *":$PREFIX/bin:"*) ;; *) echo "تنبيه: أضف $PREFIX/bin إلى PATH" ;; esac
if ! have openssl; then echo "تنبيه اختياري: OpenSSL مطلوب فقط لميزات https وwss."; fi
echo "تم! جرّب: solan --version"
