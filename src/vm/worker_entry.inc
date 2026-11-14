// =====================================================================
//  worker_entry.inc — دخول العامل الفرعي وناقل العملية (يُضمَّن بعد
//  worker_main.inc وقبل main.inc، ويعتمد worker_value.inc)
// =====================================================================
// يتصل بمنفذ الأب، يستدعي الدالة المجمّدة، ثم (وضع قناة) يبقى حياً حتى
// تُغلق القناة من الطرف الآخر. الناتج النهائي (وضع 0) يُرسل JSON سطراً.
static int worker_entry(const char *freeze_path, int port, int mode) {
  int fd = -1;
  if (port > 0) {
    for (int i = 0; i < 200 && fd < 0; i++) { fd = w_dial_local((unsigned short)port, 100); if (fd < 0) Sleep(5); }
    if (fd < 0) { fprintf(stderr, "تعذّر اتصال العامل بالأب (منفذ %d)\n", port); return 74; }
    // مهم: تهيئة حالة القناة (المهلة -1 والمخازن) — مطابق لـ fd_setup(sp[1]) في مسار POSIX
    fd_setup(fd);
  }
  int wres = worker_main(freeze_path, fd, mode);
  // POSIX يخرج فور انتهاء الدالة — نفس العقد هنا على كل المنصات
  if (fd >= 0) close(fd);
  return wres;
}

// ناقل العملية: يشغّل "cmd /c <أمر>" ويضخ stdin من سوكت، ويضخ stdout/stderr
// إلى سوكتين — يبقى حتى انتهاء الأمر.
static int proc_relay_main(const char *cmd, unsigned short pin_, unsigned short pout, unsigned short perr) {
  int fdin = -1, fdout = -1, fderr = -1;
  for (int i = 0; i < 200 && (fdin < 0 || fdout < 0 || fderr < 0); i++) {
    if (fdin < 0) fdin = w_dial_local(pin_, 100);
    if (fdout < 0) fdout = w_dial_local(pout, 100);
    if (fderr < 0) fderr = w_dial_local(perr, 100);
    if (fdin < 0 || fdout < 0 || fderr < 0) Sleep(5);
  }
  if (fdout < 0 || fderr < 0) { fprintf(stderr, "تعذّر اتصال الناقل بالأب\n"); return 74; }
  SECURITY_ATTRIBUTES sa; sa.nLength = sizeof sa; sa.lpSecurityDescriptor = NULL; sa.bInheritHandle = TRUE;
  HANDLE in_r = NULL, in_w = NULL, out_r = NULL, out_w = NULL, er_r = NULL, er_w = NULL;
  CreatePipe(&in_r, &in_w, &sa, 0); CreatePipe(&out_r, &out_w, &sa, 0); CreatePipe(&er_r, &er_w, &sa, 0);
  SetHandleInformation(in_w, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(out_r, HANDLE_FLAG_INHERIT, 0);
  SetHandleInformation(er_r, HANDLE_FLAG_INHERIT, 0);
  // CreateProcessW مع تطبيق "cmd" يفشل أحياناً (GetLastError=2) لأنه لا يبحث
  // في مسارات النظام عند تمرير سطر أوامر كامل؛ نستخدم المسار الكامل لـ cmd.exe.
  char cmdfull[1024];
  UINT syslen = GetSystemDirectoryA(cmdfull, sizeof cmdfull - 8);
  if (syslen == 0 || syslen > sizeof cmdfull - 16) strcpy(cmdfull, "C:\\Windows\\System32");
  strcat(cmdfull, "\\cmd.exe /c ");
  strcat(cmdfull, cmd);
  wchar_t *wcmd = w_utf16(cmdfull);
  STARTUPINFOW si; memset(&si, 0, sizeof si); si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES; si.hStdInput = in_r; si.hStdOutput = out_w; si.hStdError = er_w;
  PROCESS_INFORMATION pi; memset(&pi, 0, sizeof pi);
  BOOL ok = CreateProcessW(NULL, wcmd, NULL, NULL, TRUE, CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
  free(wcmd);
  CloseHandle(in_r); CloseHandle(out_w); CloseHandle(er_w);
  if (!ok) { CloseHandle(in_w); CloseHandle(out_r); CloseHandle(er_r); fprintf(stderr, "فشل بدء الأمر\n"); return 127; }  // حلقة مفردة غير حاجبة: PeekNamedPipe على الخرجين + recv غير حاجب على الدخل
  DWORD code = 1; bool done = false;
  while (!done) {
    DWORD avail = 0, got = 0;
    if (PeekNamedPipe(out_r, NULL, 0, NULL, &avail, NULL) && avail > 0) {
      char tmp[8192];
      if (ReadFile(out_r, tmp, sizeof tmp, &got, NULL) && got > 0) { size_t off = 0; while (off < got) { int w = send(fdout, tmp + off, (int)(got - off), 0); if (w <= 0) break; off += (size_t)w; } }
    }
    if (PeekNamedPipe(er_r, NULL, 0, NULL, &avail, NULL) && avail > 0) {
      char tmp[8192];
      if (ReadFile(er_r, tmp, sizeof tmp, &got, NULL) && got > 0) { size_t off = 0; while (off < got) { int w = send(fderr, tmp + off, (int)(got - off), 0); if (w <= 0) break; off += (size_t)w; } }
    }
    // دخل من الأب (إن وجد) — غير حاجب عبر fcntl الموحد (يحل fd الوهمي داخلياً)
    fcntl(fdin, F_SETFL, O_NONBLOCK);
    char itmp[4096];
    for (;;) {
      int r = (int)recv(fdin, itmp, sizeof itmp, 0);
      if (r > 0) { DWORD wrote = 0; WriteFile(in_w, itmp, (DWORD)r, &wrote, NULL); }
      else if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
      else break;
    }
    DWORD wr = WaitForSingleObject(pi.hProcess, 20);
    if (wr == WAIT_OBJECT_0) done = true;
  }
  // تصريف المتبقي
  DWORD avail = 0, got = 0;
  char tmp[8192];
  while (PeekNamedPipe(out_r, NULL, 0, NULL, &avail, NULL) && avail > 0 && ReadFile(out_r, tmp, sizeof tmp, &got, NULL) && got > 0) { size_t off = 0; while (off < got) { int w = send(fdout, tmp + off, (int)(got - off), 0); if (w <= 0) break; off += (size_t)w; } }
  while (PeekNamedPipe(er_r, NULL, 0, NULL, &avail, NULL) && avail > 0 && ReadFile(er_r, tmp, sizeof tmp, &got, NULL) && got > 0) { size_t off = 0; while (off < got) { int w = send(fderr, tmp + off, (int)(got - off), 0); if (w <= 0) break; off += (size_t)w; } }
  GetExitCodeProcess(pi.hProcess, &code);
  CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
  CloseHandle(in_w); CloseHandle(out_r); CloseHandle(er_r);
  shutdown(fdin, 2); shutdown(fdout, 2); shutdown(fderr, 2);
  close(fdin); close(fdout); close(fderr);
  return (int)code;
}
