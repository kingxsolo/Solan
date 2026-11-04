; =====================================================================
;  سولان (Solan) — مُثبِّت ويندوز احترافي
;  يثبّت solan.exe، يضيفه إلى PATH، ينشئ اختصارات، ويدعم إزالة التثبيت
;  البناء: ISCC pc\installer.iss /DExeName=dist\solan.exe
; =====================================================================
#define SolanVersion "1.1.0"
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
PrivilegesRequiredOverridesAllowed=dialog
CloseApplications=yes
SetupLogging=yes

[Languages]
Name: "arabic"; MessagesFile: "ArabicPC.isl"

[CustomMessages]
arabic.CreateDesktopIcon=إنشاء أيقونة على سطح المكتب
arabic.AddToPath=إضافة سولان إلى متغير PATH (لتشغيله من أي طرفية)
arabic.AssocFiles=ربط ملفات سولان (.سو .an .solan) بالبرنامج
arabic.RunAfter=تشغيل سولان الآن (اختبار التثبيت)
arabic.OpenReadme=فتح دليل النسخة الشخصية للويندوز
arabic.LongPaths=تفعيل دعم المسارات الطويلة في ويندوز (يتطلب صلاحيات مدير)

[Files]
Source: "{#ExeName}"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README-PC.md"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\LICENSE"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{group}\سولان (طرفية تفاعلية)"; Filename: "{app}\solan.exe"; Comment: "سولان — لغة برمجة عربية"
Name: "{group}\دليل سولان للويندوز"; Filename: "{app}\README-PC.md"
Name: "{group}\إزالة تثبيت سولان"; Filename: "{uninstallexe}"
Name: "{autodesktop}\سولان"; Filename: "{app}\solan.exe"; Tasks: desktopicon

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "أيقونات:"
Name: "addpath"; Description: "{cm:AddToPath}"; GroupDescription: "التثبيت:"; Flags: checkedonce
Name: "assoc"; Description: "{cm:AssocFiles}"; GroupDescription: "التثبيت:"; Flags: unchecked
Name: "longpaths"; Description: "{cm:LongPaths}"; GroupDescription: "التثبيت:"; Flags: unchecked oncechecked

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
Filename: "{app}\README-PC.md"; Description: "{cm:OpenReadme}"; Flags: postinstall skipifsilent shellexec

[Code]
// إضافة مجلد التثبيت إلى PATH الخاص بالمستخدم
procedure EnvAddPath(Path: string);
var
  Paths: string;
  I: Integer;
  Parts: TArray<string>;
begin
  RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths);
  Parts := Paths.Split([';']);
  for I := 0 to Length(Parts) - 1 do
    if CompareText(Trim(Parts[I]), Path) = 0 then Exit;
  if (Length(Paths) > 0) and (Paths[Length(Paths)] <> ';') then
    Paths := Paths + ';';
  Paths := Paths + Path;
  RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths);
end;

procedure EnvRemovePath(Path: string);
var
  Paths, NewPaths: string;
  I: Integer;
  Parts: TArray<string>;
begin
  if not RegQueryStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', Paths) then Exit;
  NewPaths := '';
  Parts := Paths.Split([';']);
  for I := 0 to Length(Parts) - 1 do
    if (CompareText(Trim(Parts[I]), Path) <> 0) and (Trim(Parts[I]) <> '') then
    begin
      if NewPaths <> '' then NewPaths := NewPaths + ';';
      NewPaths := NewPaths + Trim(Parts[I]);
    end;
  RegWriteStringValue(HKEY_CURRENT_USER, 'Environment', 'Path', NewPaths);
end;

procedure CurStepChanged(CurStep: TSetupStep);
begin
  if CurStep = ssPostInstall then
    if IsTaskSelected('addpath') then
      EnvAddPath(ExpandConstant('{app}'));
end;

procedure CurUninstallStepChanged(CurUninstallStep: TUninstallStep);
begin
  if CurUninstallStep = usPostUninstall then
    EnvRemovePath(ExpandConstant('{app}'));
end;
