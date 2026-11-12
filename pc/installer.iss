; =====================================================================
;  سولان (Solan) — مُثبِّت ويندوز احترافي
;  يثبّت solan.exe، يضيفه إلى PATH، ينشئ اختصارات، ويدعم إزالة التثبيت
;  البناء: ISCC pc\installer.iss "/DExeName=<مسار مطلق>\solan.exe" /DSolanVersion=1.3.1
;  (ISCC يحل المسارات النسبية نسبة إلى مجلد ملف .iss — مرّر مساراً مطلقاً للتنفيذي)
; =====================================================================
#ifndef SolanVersion
#define SolanVersion "1.3.1"
#endif
#define SolanPublisher "Solan Project"
#define SolanURL "https://github.com/kingxsolo/Solan"

#ifndef ExeName
#define ExeName "..\dist\solan.exe"
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

[Files]
Source: "{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\سولان"; Filename: "{app}\solan.exe"; Comment: "سولان — لغة برمجة عربية"
Name: "{group}\دليل سولان (README)"; Filename: "{app}\README.md"
Name: "{group}\إزالة تثبيت سولان"; Filename: "{uninstallexe}"
Name: "{autodesktop}\سولان"; Filename: "{app}\solan.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "أيقونات:"
Name: "addpath"; Description: "{cm:AddToPath}"; GroupDescription: "التثبيت:"; Flags: checkedonce
Name: "assoc"; Description: "{cm:AssocFiles}"; GroupDescription: "التثبيت:"; Flags: unchecked
Name: "longpaths"; Description: "{cm:LongPaths}"; GroupDescription: "التثبيت:"; Flags: unchecked

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

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then begin
    if WizardIsTaskSelected('addpath') then
      EnvAddPath(ExpandConstant('{app}'));
  end;
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
