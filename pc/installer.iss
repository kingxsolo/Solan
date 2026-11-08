; =====================================================================
;  سولان (Solan) — مُثبِّت ويندوز احترافي
;  يثبّت solan.exe، يضيفه إلى PATH، ينشئ اختصارات، ويدعم إزالة التثبيت
;  البناء: ISCC pc\installer.iss /DExeName=dist\solan.exe
; =====================================================================
#ifndef SolanVersion
#define SolanVersion "1.2.0"
#endif
; إصدار حزمة إضافة VS Code (مستقل عن إصدار اللغة)
#define VsixVersion "1.2.1"
#define SolanPublisher "Solan Project"
#define SolanURL "https://github.com/kingxsolo/Solan"

#ifndef ExeName
#define ExeName "dist\solan.exe"
#endif

[Setup]
AppId={{8F4A2C1E-6B7D-4E3A-9C2F-SOLANPC000001}
AppName=سولان (Solan)
AppVersion={#SolanVersion}
AppVerName=سولان {#SolanVersion}
AppPublisher={#SolanPublisher}
AppPublisherURL={#SolanURL}
AppSupportURL={#SolanURL}
DefaultDirName={autopf}\Solan
DefaultGroupName=سولان
UninstallDisplayName=سولان (Solan) {#SolanVersion}
UninstallDisplayIcon={app}\solan.exe
SetupIconFile=solan.ico
OutputDir=..\dist
OutputBaseFilename=Solan-{#SolanVersion}-Setup-x64
Compression=lzma2/max
LZMAUseSeparateProcess=yes
SolidCompression=yes
ArchitecturesInstallIn64BitMode=x64compatible
ArchitecturesAllowed=x64compatible
WizardStyle=modern
ChangesEnvironment=yes
DisableProgramGroupPage=yes
LicenseFile=..\LICENSE
PrivilegesRequired=lowest
PrivilegesRequiredOverridesAllowed=dialog commandline
CloseApplications=yes
SetupLogging=yes

[Languages]
Name: "arabic"; MessagesFile: "ArabicPC.isl"

[CustomMessages]
arabic.CreateDesktopIcon=إنشاء أيقونة على سطح المكتب
arabic.AddToPath=إضافة سولان إلى متغير PATH (لتشغيله من أي طرفية)
arabic.AssocFiles=ربط ملفات سولان (.سو .an .solan) بالبرنامج
arabic.RunAfter=تشغيل سولان الآن (اختبار التثبيت)
arabic.OpenReadme=فتح دليل اللغة (README)
arabic.LongPaths=تفعيل دعم المسارات الطويلة في ويندوز (يتطلب صلاحيات مدير)
arabic.VscodeExt=تثبيت إضافة VS Code (تلوين وقصاصات وخادم اللغة)

[Files]
Source: "{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion
; إضافة VS Code — ملفات المصدر احتياطاً إلى {app}، والتثبيت الفعلي عبر .vsix في [Code]
; حزمة .vsix تُنسخ دائماً مع المهمة (حتى بلا VS Code) لتثبيتها يدوياً لاحقاً
Source: "..\editor\vscode\*"; DestDir: "{app}\editor\vscode"; Flags: ignoreversion recursesubdirs; Tasks: vscode; Check: VscAvailable
Source: "..\dist\Solan-{#VsixVersion}.vsix"; DestDir: "{app}"; Flags: ignoreversion; Tasks: vscode

[Icons]
Name: "{group}\سولان (طرفية تفاعلية)"; Filename: "{app}\solan.exe"; Comment: "سولان — لغة برمجة عربية"
Name: "{group}\دليل سولان (README)"; Filename: "{app}\README.md"
Name: "{group}\إزالة تثبيت سولان"; Filename: "{uninstallexe}"
Name: "{autodesktop}\سولان"; Filename: "{app}\solan.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "أيقونات:"
Name: "addpath"; Description: "{cm:AddToPath}"; GroupDescription: "التثبيت:"; Flags: checkedonce
Name: "assoc"; Description: "{cm:AssocFiles}"; GroupDescription: "التثبيت:"; Flags: unchecked
Name: "longpaths"; Description: "{cm:LongPaths}"; GroupDescription: "التثبيت:"; Flags: unchecked
Name: "vscode"; Description: "{cm:VscodeExt}"; GroupDescription: "التثبيت:"

[Registry]
Root: HKCU; Subkey: "Software\Classes\.solan"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\.سو"; ValueType: string; ValueData: "Solan.Source"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\.an"; ValueType: string; ValueData: "Solan.Source"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\.solan"; ValueType: string; ValueData: "Solan.Source"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\Solan.Source"; ValueType: string; ValueData: "ملف سولان"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\Solan.Source\DefaultIcon"; ValueType: string; ValueData: "{app}\solan.exe,0"; Flags: uninsdeletekey; Tasks: assoc
Root: HKCU; Subkey: "Software\Classes\Solan.Source\shell\open\command"; ValueType: string; ValueData: """{app}\solan.exe"" ""%1"""; Flags: uninsdeletekey; Tasks: assoc
; تفعيل المسارات الطويلة (يتطلب صلاحيات مدير — يُتجاهل بصمت في التثبيت لكل مستخدم)
Root: HKLM; Subkey: "SYSTEM\CurrentControlSet\Control\FileSystem"; ValueType: dword; ValueName: "LongPathsEnabled"; ValueData: 1; Flags: uninsdeletekeyifempty; Tasks: longpaths; Check: IsAdmin

[Run]
Filename: "{app}\solan.exe"; Parameters: "-e ""اطبع('سولان مثبَّت بنجاح ✓')"""; Description: "{cm:RunAfter}"; Flags: nowait postinstall skipifsilent
Filename: "{app}\README.md"; Description: "{cm:OpenReadme}"; Flags: postinstall skipifsilent shellexec

[Code]
// إضافة مجلد التثبيت إلى PATH الخاص بالمستخدم
procedure EnvAddPath(Path: string);
var
  Paths, Remaining, Part: string;
  Found: Boolean;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths) then
    Paths := '';
  Found := False;
  Remaining := Paths;
  while Length(Remaining) > 0 do
  begin
    if Pos(';', Remaining) > 0 then
    begin
      Part := Copy(Remaining, 1, Pos(';', Remaining) - 1);
      Remaining := Copy(Remaining, Pos(';', Remaining) + 1, MaxInt);
    end
    else
    begin
      Part := Remaining;
      Remaining := '';
    end;
    if CompareText(Trim(Part), Path) = 0 then
      Found := True;
  end;
  if not Found then
  begin
    if (Length(Paths) > 0) and (Paths[Length(Paths)] <> ';') then
      Paths := Paths + ';';
    Paths := Paths + Path;
    RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths);
  end;
end;

procedure EnvRemovePath(Path: string);
var
  Paths, Remaining, Part, NewPaths: string;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths) then
    Exit;
  NewPaths := '';
  Remaining := Paths;
  while Length(Remaining) > 0 do
  begin
    if Pos(';', Remaining) > 0 then
    begin
      Part := Copy(Remaining, 1, Pos(';', Remaining) - 1);
      Remaining := Copy(Remaining, Pos(';', Remaining) + 1, MaxInt);
    end
    else
    begin
      Part := Remaining;
      Remaining := '';
    end;
    Part := Trim(Part);
    if (CompareText(Part, Path) <> 0) and (Part <> '') then
    begin
      if NewPaths <> '' then
        NewPaths := NewPaths + ';';
      NewPaths := NewPaths + Part;
    end;
  end;
  RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', NewPaths);
end;

// ============ تكامل VS Code ============
// يكتشف تثبيت VS Code ويُحدد مجلد الإضافات المناسب للمستخدم الحالي
var
  VscExtDirCached: string;
  VscChecked: Boolean;
  VscFound: Boolean;

function VscAvailable: Boolean;
var
  EnvPath: String;
begin
  if VscChecked then begin
    Result := VscFound;
    Exit;
  end;
  VscChecked := True;
  Result := False;
  // 1) تثبيت المستخدم/النظام القياسي
  if DirExists(ExpandConstant('{localappdata}\Programs\Microsoft VS Code'))
    or DirExists(ExpandConstant('{pf}\Microsoft VS Code')) then
  begin
    Result := True;
    VscFound := True;
    Exit;
  end;
  // 2) هل يوجد "bin\code.cmd" عبر PATH (المستخدم أو النظام)؟
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', EnvPath)
    and (Pos('\Microsoft VS Code\bin', AnsiUppercase(EnvPath)) > 0) then
  begin
    VscFound := True;
    Result := True;
    Exit;
  end;
  if RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', EnvPath)
    and (Pos('\Microsoft VS Code\bin', AnsiUppercase(EnvPath)) > 0) then
  begin
    VscFound := True;
    Result := True;
    Exit;
  end;
end;

function GetVscExtDir(S: string): string;
begin
  if Length(VscExtDirCached) > 0 then begin
    Result := VscExtDirCached;
    Exit;
  end;
  // مجلد إضافات VS Code الحقيقي: %USERPROFILE%\.vscode\extensions
  Result := GetEnv('USERPROFILE') + '\.vscode\extensions\solan.solan-lang-{#VsixVersion}';
  VscExtDirCached := Result;
end;

// إيجاد واجهة أوامر VS Code (code.cmd)
// يفحص المسارين القياسيين ثم يبحث في PATH (يغطي التثبيت المخصص/المحمول)
function FindCodeCli: string;
var
  EnvPath, Part, Remaining: string;
begin
  Result := '';
  if FileExists(ExpandConstant('{localappdata}\Programs\Microsoft VS Code\bin\code.cmd')) then begin
    Result := ExpandConstant('{localappdata}\Programs\Microsoft VS Code\bin\code.cmd');
    Exit;
  end;
  if FileExists(ExpandConstant('{pf}\Microsoft VS Code\bin\code.cmd')) then begin
    Result := ExpandConstant('{pf}\Microsoft VS Code\bin\code.cmd');
    Exit;
  end;
  // بحث في PATH: البيئة الحالية ثم سجل المستخدم ثم سجل النظام
  if RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', EnvPath)
    or RegQueryStringValue(HKEY_LOCAL_MACHINE, 'SYSTEM\CurrentControlSet\Control\Session Manager\Environment', 'Path', EnvPath) then begin
    Remaining := EnvPath;
    while Length(Remaining) > 0 do begin
      if Pos(';', Remaining) > 0 then begin
        Part := Copy(Remaining, 1, Pos(';', Remaining) - 1);
        Remaining := Copy(Remaining, Pos(';', Remaining) + 1, MaxInt);
      end else begin
        Part := Remaining;
        Remaining := '';
      end;
      Part := Trim(Part);
      // إزالة اقتباس محتمل حول المدخل
      if (Length(Part) > 1) and (Copy(Part, 1, 1) = '"') then
        Part := Copy(Part, 2, Length(Part) - 2);
      if (Part <> '') and FileExists(Part + '\code.cmd') then begin
        Result := Part + '\code.cmd';
        Exit;
      end;
    end;
  end;
end;

// تثبيت إضافة VS Code: الطريقة الرسمية عبر code --install-extension،
// وإن تعذر ذلك فنسخ مباشر إلى مجلد إضافات VS Code
procedure InstallVscExtension;
var
  codeCmd, vsix, extDir: string;
  resultCode: integer;
begin
  extDir := GetVscExtDir('');
  vsix := ExpandConstant('{app}\Solan-{#VsixVersion}.vsix');
  codeCmd := FindCodeCli;
  if (Length(codeCmd) > 0) and FileExists(vsix) then begin
    // علامتا اقتباس خارجيتان: قاعدة cmd /c تزيل الأولى والأخيرة من 4 اقتباسات
    // --force: إعادة الاستخراج حتى لو اعتُبرت الإضافة مثبتة (قد يكون مجلدها مفقوداً)
    if Exec(ExpandConstant('{sys}\cmd.exe'),
            '/c ""' + codeCmd + '" --install-extension "' + vsix + '" --force""',
            '', SW_HIDE, ewWaitUntilTerminated, resultCode) then
      if (resultCode = 0) and DirExists(extDir) then
        Exit;  // نجح التثبيت عبر واجهة VS Code
  end;
  // بديل: نسخ مباشر إلى مجلد إضافات VS Code
  if not DirExists(ExtractFileDir(extDir)) then
    ForceDirectories(ExtractFileDir(extDir));
  Exec(ExpandConstant('{sys}\cmd.exe'),
       '/c xcopy /e /i /y "' + ExpandConstant('{app}\editor\vscode') + '" "' + extDir + '"',
       '', SW_HIDE, ewWaitUntilTerminated, resultCode);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('addpath') then
      EnvAddPath(ExpandConstant('{app}'));
    if WizardIsTaskSelected('vscode') and VscAvailable then
      InstallVscExtension;
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
var
  extDir, codeCmd: string;
  resultCode: integer;
begin
  if CurUninstallStep = usPostUninstall then begin
    EnvRemovePath(ExpandConstant('{app}'));
    // إزالة إضافة VS Code من مكانها الحقيقي (~\.vscode\extensions)
    if VscAvailable then begin
      extDir := GetVscExtDir('');
      // إزالة التسجيل من فهرس VS Code أولاً (يمنع حالة "مسجّلة بلا ملفات")
      codeCmd := FindCodeCli;
      if Length(codeCmd) > 0 then
        Exec(ExpandConstant('{sys}\cmd.exe'),
             '/c ""' + codeCmd + '" --uninstall-extension solan.solan-lang""',
             '', SW_HIDE, ewWaitUntilTerminated, resultCode);
      // ثم ضمان حذف المجلد حتى لو فشل CLI
      DelTree(extDir, True, True, True);
    end;
  end;
end;
