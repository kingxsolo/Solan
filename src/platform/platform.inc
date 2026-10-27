#ifndef MAXFD
#define MAXFD 4096
#endif

#if defined(_WIN32)
#define SOLAN_WIN 1
#ifndef SOLAN_FORK
#define SOLAN_FORK 0
#endif
#else
#define SOLAN_WIN 0
#ifndef SOLAN_FORK
#define SOLAN_FORK 1
#endif
#endif

#if defined(__APPLE__)
#define SOLAN_MAC 1
#else
#define SOLAN_MAC 0
#endif

static const char *g_argv0;
#if !SOLAN_WIN

#include <sys/stat.h>
#include <dirent.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <poll.h>
#include <fcntl.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/resource.h>
#if defined(__GLIBC__)
#include <malloc.h>
#else
#define mallopt(a, b) 0
#define M_MMAP_THRESHOLD 0
#define M_TRIM_THRESHOLD 0
#endif
#if defined(__linux__) || defined(__APPLE__)
#include <sys/mman.h>
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

typedef struct stat sstat_t;
#define sstat(p, s) stat(p, s)
#define FDSYS(fd) (fd)
#define sk_unsupported() ((void)0)
#define SOLAN_LIBEXT_NATIVE 1

static bool os_random(void *buf, size_t n) {
  FILE *f = fopen("/dev/urandom", "rb"); if (!f) return false;
  size_t g = fread(buf, 1, n, f); fclose(f); return g == n;
}
static const char *os_tmpdir(void) { const char *t = getenv("TMPDIR"); return t && *t ? t : "/tmp"; }
static int os_mkstemp(char *tmpl) { return mkstemp(tmpl); }
static void os_console_init(void) {}
static void os_ignore_sigpipe(void) { signal(SIGPIPE, SIG_IGN); }
#define solan_strptime strptime
static void os_args_utf8(int *argc, char ***argv) { (void)argc; (void)argv; }
static void os_set_memlimit(long mb) {
  struct rlimit rl; rl.rlim_cur = rl.rlim_max = (rlim_t)mb * 1024 * 1024; setrlimit(RLIMIT_AS, &rl);
}
static void os_set_timeout(unsigned sec, void (*h)(int)) { signal(SIGALRM, h); alarm(sec); }
static const char *os_devtty(void) { return "/dev/tty"; }
static bool os_is_abs(const char *p) { return p[0] == '/'; }
static const char *os_name(void) {
#if defined(__APPLE__)
  return "macos";
#elif defined(__ANDROID__)
  return "android";
#elif defined(__linux__)
  return "linux";
#elif defined(__FreeBSD__)
  return "freebsd";
#elif defined(__OpenBSD__)
  return "openbsd";
#elif defined(__NetBSD__)
  return "netbsd";
#else
  return "unix";
#endif
}
#include <pwd.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif
static bool os_user_home(char *out, size_t sz) {
  const char *h = getenv("HOME");
  if (h && *h) { snprintf(out, sz, "%s", h); return true; }
  struct passwd *pw = getpwuid(getuid());
  if (pw && pw->pw_dir && *pw->pw_dir) { snprintf(out, sz, "%s", pw->pw_dir); return true; }
  return false;
}
static bool os_can_write_dir(const char *p) { return access(p, W_OK | X_OK) == 0; }
static void os_user_tag(char *out, size_t sz) { snprintf(out, sz, "%lu", (unsigned long)getuid()); }
static bool os_self_exe(char *out, size_t sz) {
#if defined(__APPLE__)
  char tmp[PATH_MAX]; uint32_t n = (uint32_t)sizeof tmp;
  if (_NSGetExecutablePath(tmp, &n) == 0) { char rp[PATH_MAX]; snprintf(out, sz, "%s", realpath(tmp, rp) ? rp : tmp); return true; }
#else
  static const char *links[] = {"/proc/self/exe", "/proc/curproc/file", "/proc/self/path/a.out", NULL};
  for (int i = 0; links[i]; i++) { ssize_t n = readlink(links[i], out, sz - 1); if (n > 0) { out[n] = 0; return true; } }
#endif
  if (!g_argv0 || !*g_argv0) return false;
  char rp[PATH_MAX];
  if (strchr(g_argv0, '/')) { if (realpath(g_argv0, rp)) { snprintf(out, sz, "%s", rp); return true; } return false; }
  const char *path = getenv("PATH"); if (!path) return false;
  char *dup = strdup(path), *save = NULL;
  for (char *d = strtok_r(dup, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
    char cand[PATH_MAX]; snprintf(cand, sizeof cand, "%s/%s", *d ? d : ".", g_argv0);
    if (access(cand, X_OK) == 0 && realpath(cand, rp)) { snprintf(out, sz, "%s", rp); free(dup); return true; }
  }
  free(dup); return false;
}
static int os_run(char *const *argv, const char *cwd, char **out, size_t *outn) {
#if !SOLAN_FORK
  (void)argv; (void)cwd; (void)out; (void)outn; return -1;
#else
  int pfd[2] = {-1, -1};
  if (out && pipe(pfd) < 0) return -1;
  fflush(stdout); fflush(stderr);
  pid_t pid = fork(); if (pid < 0) { if (out) { close(pfd[0]); close(pfd[1]); } return -1; }
  if (pid == 0) {
    int dn = open("/dev/null", O_RDWR);
    if (out) { dup2(pfd[1], 1); close(pfd[0]); close(pfd[1]); if (dn >= 0) { dup2(dn, 0); dup2(dn, 2); } }
    if (dn >= 0) close(dn);
    if (cwd && chdir(cwd) != 0) _exit(126);
    setenv("GIT_TERMINAL_PROMPT", "0", 1); setenv("GIT_ALLOW_PROTOCOL", "https:http:ssh:git:file", 1);
    setenv("GIT_HTTP_LOW_SPEED_LIMIT", "1000", 0); setenv("GIT_HTTP_LOW_SPEED_TIME", "30", 0);
    setenv("GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o ConnectTimeout=20", 0);
    execvp(argv[0], argv); _exit(127);
  }
  if (out) {
    close(pfd[1]); size_t cap = 4096, n = 0; char *b = (char *)malloc(cap); char tmp[8192]; ssize_t r;
    while ((r = read(pfd[0], tmp, sizeof tmp)) > 0 || (r < 0 && errno == EINTR)) {
      if (r <= 0) continue;
      if (n + (size_t)r + 1 > cap) { while (n + (size_t)r + 1 > cap) cap *= 2; b = (char *)realloc(b, cap); }
      memcpy(b + n, tmp, (size_t)r); n += (size_t)r;
    }
    close(pfd[0]); b[n] = 0; *out = b; if (outn) *outn = n;
  }
  int st = 0; while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
  return WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
#endif
}
static bool os_is_link(const char *p) { struct stat st; return lstat(p, &st) == 0 && S_ISLNK(st.st_mode); }
static int os_rm_file(const char *p) { return unlink(p); }
#define OS_PATHSEP '/'
#define OS_NULLDEV "/dev/null"

#else

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <io.h>
#include <direct.h>
#include <process.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <shellapi.h>

#undef PATH_MAX
#define PATH_MAX 4096
#ifndef SIGPIPE
#define SIGPIPE 13
#endif
#ifndef SIGALRM
#define SIGALRM 14
#endif
#ifndef SIGCHLD
#define SIGCHLD 17
#endif
#define MSG_NOSIGNAL 0
#define RTLD_NOW 0
#define RTLD_LAZY 0
#define RTLD_GLOBAL 0
#define RTLD_DEFAULT ((void *)0)
#ifndef F_GETFL
#define F_GETFL 3
#define F_SETFL 4
#endif
#undef O_NONBLOCK
#define O_NONBLOCK 0x100000
#ifndef EINPROGRESS
#define EINPROGRESS 112
#endif
#ifndef ETIMEDOUT
#define ETIMEDOUT 138
#endif
#ifndef CLOCK_REALTIME
#define CLOCK_REALTIME 0
#endif
#ifndef CLOCK_MONOTONIC
#define CLOCK_MONOTONIC 1
#endif
#define WIFEXITED(x) 1
#define WEXITSTATUS(x) (x)
#define WTERMSIG(x) 0
#define sk_unsupported() ((void)0)
#define FDSYS(fd) ((int)w_s(fd))
#define OS_PATHSEP '/'
#define OS_NULLDEV "NUL"

static wchar_t *w_utf16(const char *s) {
  int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0); if (n <= 0) n = 1;
  wchar_t *w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t)); if (!w) abort();
  if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) w[0] = 0;
  return w;
}
static char *w_utf8(const wchar_t *w, char *out, size_t cap) {
  if (!WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)cap, NULL, NULL)) out[0] = 0;
  return out;
}
static void w_slashes(char *p) { for (; *p; p++) if (*p == '\\') *p = '/'; }

static FILE *w_fopen(const char *p, const char *m) {
  wchar_t *wp = w_utf16(p), *wm = w_utf16(m); FILE *f = _wfopen(wp, wm); free(wp); free(wm); return f;
}
#define fopen w_fopen
typedef struct _stat64 sstat_t;
static int w_stat(const char *p, sstat_t *st) { wchar_t *w = w_utf16(p); int r = _wstat64(w, st); free(w); return r; }
#define sstat(p, s) w_stat(p, s)
static char *w_realpath(const char *p, char *out) {
  wchar_t *w = w_utf16(p), full[PATH_MAX]; DWORD n = GetFullPathNameW(w, PATH_MAX, full, NULL); free(w);
  if (!n || n >= PATH_MAX || GetFileAttributesW(full) == INVALID_FILE_ATTRIBUTES) return NULL;
  w_utf8(full, out, PATH_MAX); w_slashes(out); return out;
}
#define realpath(p, o) w_realpath(p, o)
static char *w_getcwd(char *b, size_t n) {
  wchar_t w[PATH_MAX]; DWORD r = GetCurrentDirectoryW(PATH_MAX, w); if (!r || r >= PATH_MAX) return NULL;
  char tmp[PATH_MAX]; w_utf8(w, tmp, sizeof tmp); w_slashes(tmp); if (strlen(tmp) >= n) return NULL; strcpy(b, tmp); return b;
}
#define getcwd(b, n) w_getcwd(b, n)
static int w_mkdir(const char *p, int mode) { (void)mode; wchar_t *w = w_utf16(p); int r = _wmkdir(w); free(w); return r; }
#define mkdir(p, m) w_mkdir(p, m)
static bool os_is_link(const char *p) { wchar_t *w = w_utf16(p); DWORD a = GetFileAttributesW(w); free(w); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_REPARSE_POINT); }
static int os_rm_file(const char *p) { wchar_t *w = w_utf16(p); int r = DeleteFileW(w) ? 0 : -1; if (r) { DWORD a = GetFileAttributesW(w); if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_READONLY)) { SetFileAttributesW(w, a & ~FILE_ATTRIBUTE_READONLY); r = DeleteFileW(w) ? 0 : -1; } } free(w); return r; }
static bool os_user_home(char *out, size_t sz) {
  const char *h = getenv("USERPROFILE");
  if (h && *h) { snprintf(out, sz, "%s", h); w_slashes(out); return true; }
  const char *d = getenv("HOMEDRIVE"), *p = getenv("HOMEPATH");
  if (d && p) { snprintf(out, sz, "%s%s", d, p); w_slashes(out); return true; }
  return false;
}
static bool os_can_write_dir(const char *p) { wchar_t *w = w_utf16(p); DWORD a = GetFileAttributesW(w); free(w); return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY) && !(a & FILE_ATTRIBUTE_READONLY); }
static void os_user_tag(char *out, size_t sz) { const char *u = getenv("USERNAME"); snprintf(out, sz, "%s", u && *u ? u : "user"); }
static bool os_self_exe(char *out, size_t sz) {
  wchar_t w[PATH_MAX]; DWORD n = GetModuleFileNameW(NULL, w, PATH_MAX);
  if (!n || n >= PATH_MAX) return false;
  w_utf8(w, out, sz); w_slashes(out); return true;
}
static void w_append_arg(char **buf, size_t *n, size_t *cap, const char *a) {
  size_t need = strlen(a) * 2 + 8;
  if (*n + need + 1 > *cap) { *cap = (*n + need + 1) * 2; *buf = (char *)realloc(*buf, *cap); }
  char *o = *buf + *n;
  if (*n) *o++ = ' ';
  if (*a && !strpbrk(a, " \t\n\v\"")) { size_t l = strlen(a); memcpy(o, a, l); o += l; }
  else {
    *o++ = '"';
    for (const char *c = a;; c++) {
      size_t bs = 0;
      while (*c == '\\') { bs++; c++; }
      if (!*c) { for (size_t k = 0; k < bs * 2; k++) *o++ = '\\'; break; }
      if (*c == '"') { for (size_t k = 0; k < bs * 2 + 1; k++) *o++ = '\\'; *o++ = '"'; }
      else { for (size_t k = 0; k < bs; k++) *o++ = '\\'; *o++ = *c; }
    }
    *o++ = '"';
  }
  *o = 0; *n = (size_t)(o - *buf);
}
static int os_run(char *const *argv, const char *cwd, char **out, size_t *outn) {
  char *cl = NULL; size_t n = 0, cap = 0;
  for (int i = 0; argv[i]; i++) w_append_arg(&cl, &n, &cap, argv[i]);
  if (!cl) return -1;
  wchar_t *wcl = w_utf16(cl), *wcwd = cwd ? w_utf16(cwd) : NULL; free(cl);
  _putenv_s("GIT_TERMINAL_PROMPT", "0"); _putenv_s("GIT_ALLOW_PROTOCOL", "https:http:ssh:git:file");
  if (!getenv("GIT_HTTP_LOW_SPEED_LIMIT")) { _putenv_s("GIT_HTTP_LOW_SPEED_LIMIT", "1000"); _putenv_s("GIT_HTTP_LOW_SPEED_TIME", "30"); }
  if (!getenv("GIT_SSH_COMMAND")) _putenv_s("GIT_SSH_COMMAND", "ssh -o BatchMode=yes -o ConnectTimeout=20");
  SECURITY_ATTRIBUTES sa; sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
  HANDLE rd = NULL, wr = NULL, nul = INVALID_HANDLE_VALUE;
  STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
  if (out) {
    if (!CreatePipe(&rd, &wr, &sa, 0)) { free(wcl); free(wcwd); return -1; }
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    nul = CreateFileW(L"NUL", GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, NULL);
    si.dwFlags = STARTF_USESTDHANDLES; si.hStdOutput = wr; si.hStdError = nul; si.hStdInput = nul;
  }
  PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
  fflush(stdout); fflush(stderr);
  BOOL ok = CreateProcessW(NULL, wcl, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, wcwd, &si, &pi);
  free(wcl); free(wcwd);
  if (out) { CloseHandle(wr); if (nul != INVALID_HANDLE_VALUE) CloseHandle(nul); }
  if (!ok) { if (out) { CloseHandle(rd); *out = (char *)calloc(1, 1); if (outn) *outn = 0; } return 127; }
  if (out) {
    size_t c2 = 4096, k = 0; char *b = (char *)malloc(c2); DWORD got; char tmp[8192];
    while (ReadFile(rd, tmp, sizeof tmp, &got, NULL) && got > 0) {
      if (k + got + 1 > c2) { while (k + got + 1 > c2) c2 *= 2; b = (char *)realloc(b, c2); }
      memcpy(b + k, tmp, got); k += got;
    }
    CloseHandle(rd); b[k] = 0; *out = b; if (outn) *outn = k;
  }
  WaitForSingleObject(pi.hProcess, INFINITE);
  DWORD code = 1; GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  return (int)code;
}
#define isatty _isatty
#define popen _popen
#define pclose _pclose
struct dirent { char d_name[1024]; };
typedef struct { HANDLE h; WIN32_FIND_DATAW fd; int first; struct dirent e; } DIR;
static DIR *opendir(const char *p) {
  size_t L = strlen(p); char *pat = (char *)malloc(L + 3); memcpy(pat, p, L); if (L && p[L - 1] != '/' && p[L - 1] != '\\') pat[L++] = '/'; pat[L++] = '*'; pat[L] = 0;
  wchar_t *w = w_utf16(pat); free(pat); DIR *d = (DIR *)calloc(1, sizeof *d); d->h = FindFirstFileW(w, &d->fd); free(w);
  if (d->h == INVALID_HANDLE_VALUE) { free(d); return NULL; } d->first = 1; return d;
}
static struct dirent *readdir(DIR *d) {
  if (d->first) d->first = 0; else if (!FindNextFileW(d->h, &d->fd)) return NULL;
  w_utf8(d->fd.cFileName, d->e.d_name, sizeof d->e.d_name); return &d->e;
}
static int closedir(DIR *d) { FindClose(d->h); free(d); return 0; }

static int w_setenv(const char *k, const char *v, int ow) { (void)ow; return _putenv_s(k, v); }
#define setenv(k, v, o) w_setenv(k, v, o)
static int w_clock_gettime(int id, struct timespec *ts) {
  if (id == CLOCK_MONOTONIC) {
    LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    ts->tv_sec = (time_t)(c.QuadPart / f.QuadPart); ts->tv_nsec = (long)((c.QuadPart % f.QuadPart) * 1000000000LL / f.QuadPart);
  } else {
    FILETIME ft; GetSystemTimePreciseAsFileTime(&ft);
    unsigned long long t = (((unsigned long long)ft.dwHighDateTime << 32) | ft.dwLowDateTime) - 116444736000000000ULL;
    ts->tv_sec = (time_t)(t / 10000000ULL); ts->tv_nsec = (long)((t % 10000000ULL) * 100);
  }
  return 0;
}
#define clock_gettime(i, t) w_clock_gettime(i, t)
static int w_nanosleep(const struct timespec *ts, struct timespec *rem) {
  (void)rem; unsigned long long ms = (unsigned long long)ts->tv_sec * 1000ULL + (unsigned long long)ts->tv_nsec / 1000000ULL; Sleep((DWORD)ms); return 0;
}
#define nanosleep(a, b) w_nanosleep(a, b)
#define localtime_r(t, r) (localtime_s(r, t) ? NULL : (r))
#define gmtime_r(t, r) (gmtime_s(r, t) ? NULL : (r))
#define timegm(tm) _mkgmtime(tm)
static void *memmem(const void *h, size_t hl, const void *n, size_t nl) {
  if (!nl) return (void *)h; if (hl < nl) return NULL; const char *p = (const char *)h, *e = p + hl - nl;
  for (; p <= e; p++) if (*p == *(const char *)n && !memcmp(p, n, nl)) return (void *)p; return NULL;
}
static ssize_t w_getline(char **lp, size_t *n, FILE *f) {
  size_t len = 0; int c; if (!*lp || !*n) { *n = 128; *lp = (char *)malloc(*n); if (!*lp) return -1; }
  while ((c = fgetc(f)) != EOF) {
    if (len + 2 > *n) { *n *= 2; char *t = (char *)realloc(*lp, *n); if (!t) return -1; *lp = t; }
    (*lp)[len++] = (char)c; if (c == '\n') break;
  }
  if (!len) return -1; (*lp)[len] = 0; return (ssize_t)len;
}
#define getline(a, b, c) w_getline(a, b, c)

static void *dlopen(const char *n, int f) { (void)f; wchar_t *w = w_utf16(n); HMODULE h = LoadLibraryW(w); free(w); return (void *)h; }
static void *dlsym(void *h, const char *s) { if (!h) h = (void *)GetModuleHandleA(NULL); return (void *)GetProcAddress((HMODULE)h, s); }
static int dlclose(void *h) { return !FreeLibrary((HMODULE)h); }
static const char *dlerror(void) { return "LoadLibrary failed"; }

static SOCKET w_tab[MAXFD]; static unsigned char w_nb[MAXFD]; static int w_wsa_done;
static void w_wsa(void) { if (!w_wsa_done) { WSADATA d; WSAStartup(MAKEWORD(2, 2), &d); w_wsa_done = 1; } }
static int w_reg(SOCKET s) {
  if (s == INVALID_SOCKET) return -1;
  for (int i = 3; i < MAXFD; i++) if (!w_tab[i]) { w_tab[i] = s; w_nb[i] = 0; return i; }
  closesocket(s); errno = EMFILE; return -1;
}
static SOCKET w_s(int fd) { return (fd >= 3 && fd < MAXFD && w_tab[fd]) ? w_tab[fd] : INVALID_SOCKET; }
static void w_seterr(void) {
  switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK: errno = EWOULDBLOCK; break;
    case WSAEINTR: errno = EINTR; break;
    case WSAETIMEDOUT: errno = ETIMEDOUT; break;
    case WSAECONNRESET: errno = ECONNRESET; break;
    case WSAECONNREFUSED: errno = ECONNREFUSED; break;
    case WSAEADDRINUSE: errno = EADDRINUSE; break;
    case WSAENOTCONN: errno = ENOTCONN; break;
    default: errno = EIO; break;
  }
}
static int w_socket(int d, int t, int p) { w_wsa(); SOCKET s = socket(d, t, p); if (s == INVALID_SOCKET) { w_seterr(); return -1; } return w_reg(s); }
static int w_accept(int fd, struct sockaddr *a, socklen_t *l) { SOCKET c = accept(w_s(fd), a, l); if (c == INVALID_SOCKET) { w_seterr(); return -1; } return w_reg(c); }
static int w_close(int fd) {
  if (fd >= 3 && fd < MAXFD && w_tab[fd]) { closesocket(w_tab[fd]); w_tab[fd] = 0; return 0; }
  return _close(fd);
}
static int w_connect(int fd, const struct sockaddr *a, socklen_t l) {
  int r = connect(w_s(fd), a, l); if (r) { if (WSAGetLastError() == WSAEWOULDBLOCK) errno = EINPROGRESS; else w_seterr(); } return r;
}
static int w_bind(int fd, const struct sockaddr *a, socklen_t l) { int r = bind(w_s(fd), a, l); if (r) w_seterr(); return r; }
static int w_listen(int fd, int b) { int r = listen(w_s(fd), b); if (r) w_seterr(); return r; }
static int w_setsockopt(int fd, int lv, int nm, const void *v, socklen_t l) { int r = setsockopt(w_s(fd), lv, nm, (const char *)v, l); if (r) w_seterr(); return r; }
static int w_getsockopt(int fd, int lv, int nm, void *v, socklen_t *l) { int r = getsockopt(w_s(fd), lv, nm, (char *)v, l); if (r) w_seterr(); return r; }
static int w_shutdown(int fd, int how) { return shutdown(w_s(fd), how); }
static ssize_t w_recv(int fd, void *b, size_t n, int f) { int r = recv(w_s(fd), (char *)b, (int)(n > 0x40000000 ? 0x40000000 : n), f); if (r < 0) w_seterr(); return r; }
static ssize_t w_send(int fd, const void *b, size_t n, int f) { int r = send(w_s(fd), (const char *)b, (int)(n > 0x40000000 ? 0x40000000 : n), f); if (r < 0) w_seterr(); return r; }
static ssize_t w_read(int fd, void *b, size_t n) { if (w_s(fd) != INVALID_SOCKET) return w_recv(fd, b, n, 0); return _read(fd, b, (unsigned)n); }
static ssize_t w_write(int fd, const void *b, size_t n) { if (w_s(fd) != INVALID_SOCKET) return w_send(fd, b, n, 0); return _write(fd, b, (unsigned)n); }
static int w_fcntl(int fd, int cmd, ...) {
  if (cmd == F_GETFL) return (fd >= 3 && fd < MAXFD && w_nb[fd]) ? O_NONBLOCK : 0;
  if (cmd == F_SETFL) {
    va_list ap; va_start(ap, cmd); int fl = va_arg(ap, int); va_end(ap);
    u_long m = (fl & O_NONBLOCK) ? 1 : 0; if (ioctlsocket(w_s(fd), FIONBIO, &m)) return -1; w_nb[fd] = (unsigned char)m; return 0;
  }
  return -1;
}
static int w_poll(struct pollfd *p, unsigned n, int t) {
  w_wsa(); SOCKET *sv = (SOCKET *)malloc((n + 1) * sizeof(SOCKET)); int valid = 0;
  for (unsigned i = 0; i < n; i++) { sv[i] = (SOCKET)p[i].fd; SOCKET s = p[i].fd >= 0 ? w_s((int)p[i].fd) : INVALID_SOCKET; p[i].fd = s; if (s != INVALID_SOCKET) valid++; p[i].revents = 0; }
  int r;
  if (!valid) { if (t > 0) Sleep((DWORD)t); else if (t < 0) Sleep(INFINITE); r = 0; }
  else { r = WSAPoll(p, (ULONG)n, t); if (r < 0) w_seterr(); }
  for (unsigned i = 0; i < n; i++) p[i].fd = (SOCKET)sv[i];
  free(sv); return r;
}
#define socket(a, b, c) w_socket(a, b, c)
#define accept(f, a, l) w_accept(f, a, l)
#define close(f) w_close(f)
#define connect(f, a, l) w_connect(f, a, l)
#define bind(f, a, l) w_bind(f, a, l)
#define listen(f, b) w_listen(f, b)
#define setsockopt(f, l, n, v, s) w_setsockopt(f, l, n, v, s)
#define getsockopt(f, l, n, v, s) w_getsockopt(f, l, n, v, s)
#define shutdown(f, h) w_shutdown(f, h)
#define recv(f, b, n, fl) w_recv(f, b, n, fl)
#define send(f, b, n, fl) w_send(f, b, n, fl)
#define read(f, b, n) w_read(f, b, n)
#define write(f, b, n) w_write(f, b, n)
#define fcntl w_fcntl
#define poll(p, n, t) w_poll(p, n, t)

static int kill(int pid, int sig) { (void)sig; HANDLE h = OpenProcess(PROCESS_TERMINATE, FALSE, (DWORD)pid); if (!h) return -1; BOOL ok = TerminateProcess(h, 1); CloseHandle(h); return ok ? 0 : -1; }

static void (*w_alarm_h)(int);
static DWORD WINAPI w_alarm_thread(LPVOID p) { Sleep((DWORD)(uintptr_t)p * 1000); if (w_alarm_h) w_alarm_h(SIGALRM); ExitProcess(124); return 0; }
static void os_set_timeout(unsigned sec, void (*h)(int)) { w_alarm_h = h; HANDLE t = CreateThread(NULL, 0, w_alarm_thread, (LPVOID)(uintptr_t)sec, 0, NULL); if (t) CloseHandle(t); }
static void os_set_memlimit(long mb) {
  HANDLE job = CreateJobObjectW(NULL, NULL); if (!job) return;
  JOBOBJECT_EXTENDED_LIMIT_INFORMATION li; memset(&li, 0, sizeof li);
  li.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_PROCESS_MEMORY; li.ProcessMemoryLimit = (SIZE_T)mb * 1024 * 1024;
  SetInformationJobObject(job, JobObjectExtendedLimitInformation, &li, sizeof li); AssignProcessToJobObject(job, GetCurrentProcess());
}

BOOLEAN NTAPI SystemFunction036(PVOID, ULONG);
static bool os_random(void *buf, size_t n) { return SystemFunction036(buf, (ULONG)n) != 0; }
static const char *os_tmpdir(void) {
  static char t[PATH_MAX]; if (t[0]) return t; wchar_t w[PATH_MAX]; DWORD r = GetTempPathW(PATH_MAX, w);
  if (!r || r >= PATH_MAX) { strcpy(t, "."); return t; } w_utf8(w, t, sizeof t); w_slashes(t); size_t L = strlen(t); if (L > 1 && t[L - 1] == '/') t[L - 1] = 0; return t;
}
static int os_mkstemp(char *tmpl) {
  size_t L = strlen(tmpl); if (L < 6) return -1; unsigned char r[6]; os_random(r, 6);
  for (int i = 0; i < 6; i++) tmpl[L - 6 + i] = "abcdefghijklmnopqrstuvwxyz0123456789"[r[i] % 36];
  wchar_t *w = w_utf16(tmpl); int fd = _wopen(w, _O_RDWR | _O_CREAT | _O_EXCL | _O_BINARY, _S_IREAD | _S_IWRITE); free(w); return fd;
}
static void os_ignore_sigpipe(void) {}
static void os_console_init(void) {
  SetConsoleOutputCP(CP_UTF8); SetConsoleCP(CP_UTF8);
  _setmode(_fileno(stdout), _O_BINARY); _setmode(_fileno(stderr), _O_BINARY);
  DWORD m; HANDLE h[2] = {GetStdHandle(STD_OUTPUT_HANDLE), GetStdHandle(STD_ERROR_HANDLE)};
  for (int i = 0; i < 2; i++) if (h[i] != INVALID_HANDLE_VALUE && GetConsoleMode(h[i], &m)) SetConsoleMode(h[i], m | 0x0004  );
  if (!getenv("HOME")) { const char *u = getenv("USERPROFILE"); if (u) _putenv_s("HOME", u); }
}
static void os_args_utf8(int *argc, char ***argv) {
  int n = 0; wchar_t **w = CommandLineToArgvW(GetCommandLineW(), &n); if (!w) return;
  char **v = (char **)calloc((size_t)n + 1, sizeof(char *));
  for (int i = 0; i < n; i++) { int L = WideCharToMultiByte(CP_UTF8, 0, w[i], -1, NULL, 0, NULL, NULL); v[i] = (char *)malloc((size_t)L + 1); WideCharToMultiByte(CP_UTF8, 0, w[i], -1, v[i], L, NULL, NULL); }
  LocalFree(w); *argc = n; *argv = v;
}
static const char *os_devtty(void) { return "CONIN$"; }
static bool os_is_abs(const char *p) { return p[0] == '/' || p[0] == '\\' || (((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':'); }
static const char *os_name(void) { return "windows"; }

static const char *solan_strptime(const char *s, const char *f, struct tm *tm) {
  for (; *f; f++) {
    if (*f == '%' && f[1]) {
      f++; int w = 0, v = 0, mx = 2, lo = 0, hi = 0; int *dst = NULL; int off = 0;
      switch (*f) {
        case 'Y': mx = 4; dst = &tm->tm_year; off = -1900; lo = 0; hi = 9999; break;
        case 'y': dst = &tm->tm_year; lo = 0; hi = 99; break;
        case 'm': dst = &tm->tm_mon; off = -1; lo = 1; hi = 12; break;
        case 'd': dst = &tm->tm_mday; lo = 1; hi = 31; break;
        case 'H': dst = &tm->tm_hour; lo = 0; hi = 23; break;
        case 'M': dst = &tm->tm_min; lo = 0; hi = 59; break;
        case 'S': dst = &tm->tm_sec; lo = 0; hi = 61; break;
        case '%': if (*s++ != '%') return NULL; continue;
        default: return NULL;
      }
      while (w < mx && *s >= '0' && *s <= '9') { v = v * 10 + (*s++ - '0'); w++; }
      if (!w || v < lo || v > hi) return NULL;
      if (*f == 'y') v += v < 69 ? 100 : 0; else v += off;
      *dst = v;
    } else if (isspace((unsigned char)*f)) { while (isspace((unsigned char)*s)) s++; }
    else if (*s++ != *f) return NULL;
  }
  return s;
}
#endif

#if SOLAN_WIN
#define OS_DLLEXT ".dll"
#elif SOLAN_MAC
#define OS_DLLEXT ".dylib"
#else
#define OS_DLLEXT ".so"
#endif

static const char *os_arch(void) {
#if defined(__x86_64__) || defined(_M_X64)
  return "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
  return "arm64";
#elif defined(__arm__)
  return "arm";
#elif defined(__riscv)
  return "riscv";
#elif defined(__i386__)
  return "x86";
#else
  return "unknown";
#endif
}
