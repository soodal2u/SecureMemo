; Inno Setup script — 보안 메모 (SecureMemo)
; Build: ISCC installer\SecureMemo.iss

#define MyAppName "보안 메모"
#define MyAppNameEn "SecureMemo"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "SecureMemo"
#define MyAppExeName "SecureMemo.exe"

[Setup]
AppId={{A7C3E91F-2B4D-4E8A-9F11-SECUREMEMO001}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
DefaultDirName={autopf}\{#MyAppNameEn}
DefaultGroupName={#MyAppName}
DisableProgramGroupPage=yes
OutputDir=..\dist
OutputBaseFilename=SecureMemo_Setup
SetupIconFile=..\resources\app.ico
UninstallDisplayIcon={app}\{#MyAppExeName}
Compression=lzma2
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=lowest
ArchitecturesInstallIn64BitMode=x64compatible
; Korean UI where possible
ShowLanguageDialog=no

[Languages]
Name: "korean"; MessagesFile: "compiler:Languages\Korean.isl"
Name: "english"; MessagesFile: "compiler:Default.isl"

[Tasks]
Name: "desktopicon"; Description: "바탕화면 바로가기 만들기"; GroupDescription: "추가 아이콘:"; Flags: unchecked
Name: "startup"; Description: "Windows 시작 시 자동 실행"; GroupDescription: "시작 프로그램:"; Flags: checkedonce

[Files]
Source: "..\out\SecureMemo.exe"; DestDir: "{app}"; Flags: ignoreversion
Source: "..\README.md"; DestDir: "{app}"; Flags: ignoreversion skipifsourcedoesntexist

[Icons]
Name: "{group}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; IconFilename: "{app}\{#MyAppExeName}"
Name: "{group}\{#MyAppName} 제거"; Filename: "{uninstallexe}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon; IconFilename: "{app}\{#MyAppExeName}"
; Startup folder shortcut when task selected
Name: "{userstartup}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: startup; IconFilename: "{app}\{#MyAppExeName}"

[Registry]
; Also register Run key for startup (backup to Startup folder)
Root: HKCU; Subkey: "Software\Microsoft\Windows\CurrentVersion\Run"; ValueType: string; ValueName: "SecureMemo"; ValueData: """{app}\{#MyAppExeName}"""; Flags: uninsdeletevalue; Tasks: startup

[Run]
Filename: "{app}\{#MyAppExeName}"; Description: "{#MyAppName} 실행"; Flags: nowait postinstall skipifsilent

[UninstallDelete]
Type: filesandordirs; Name: "{localappdata}\SecureMemo"
; Note: vault is under %APPDATA%\SecureMemo — keep user data by default
; Uncomment next line to wipe encrypted notes on uninstall:
; Type: filesandordirs; Name: "{userappdata}\SecureMemo"

[Code]
function InitializeUninstall(): Boolean;
begin
  Result := True;
  if MsgBox('암호화된 메모 데이터(%APPDATA%\SecureMemo)도 삭제할까요?'#13#10 +
            '아니오를 선택하면 메모는 남겨 둡니다.',
            mbConfirmation, MB_YESNO) = IDYES then
  begin
    DelTree(ExpandConstant('{userappdata}\SecureMemo'), True, True, True);
  end;
end;
