# 보안 메모 (SecureMemo)

Windows용 **암호화 스티키 메모** 앱입니다.  
Windows Sticky Notes와 별개로 동작하며, 메모는 디스크에 **AES-256-GCM** 으로 저장됩니다.

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows-blue.svg)](#)

## Features

- 마스터 비밀번호로 잠금 / 잠금 해제
- 목록 창 + 다중 스티키 메모 창 (다크 UI)
- 노트 본문 AES-256-GCM 암호화 저장 (`%APPDATA%\SecureMemo\vault.dat`)
- PBKDF2-HMAC-SHA256 (고반복) 키 유도
- 자동 잠금 (5분 ~ 6시간 / 끄기)
- 단일 인스턴스 (중복 실행으로 vault 덮어쓰기 방지)
- 열려 있던 노트 위치·상태 복원
- 시스템 트레이 상주 / 시작 프로그램 등록 (설치 옵션)
- RichEdit 서식: 굵게, 기울임, 밑줄, 취소선, 글머리, 이미지

## Download

**Release 설치 파일:** [Releases](https://github.com/soodal2u/SecureMemo/releases)

`SecureMemo_Setup.exe` 를 받아 설치하세요.

## Build (Windows)

### Requirements

- Windows 10/11
- [LLVM-MinGW](https://github.com/mstorsjo/llvm-mingw) 또는 호환 clang++ / windres
- (설치 패키지) [Inno Setup 6](https://jrsoftware.org/isinfo.php)

### Compile

```bat
compile.bat
```

결과: `out\SecureMemo.exe`

### Installer

```bat
build_installer.bat
```

결과: `dist\SecureMemo_Setup.exe`

## Usage

1. 실행 후 마스터 비밀번호 설정 (최초 1회)
2. 목록에서 메모 열기 / `+` 로 새 메모
3. ⚙ 설정 → 자동 잠금 시간, 비밀번호 변경, 잠금, 종료
4. 목록 ✕ → 목록만 숨김 (잠금 아님)
5. 잠금 → 본문 암호화 유지, 잠금 화면 표시

## Data location

| File | Path | Contents |
|------|------|----------|
| `vault.dat` | `%APPDATA%\SecureMemo\` | Encrypted notes (AES-256-GCM) |
| `public.idx` | `%APPDATA%\SecureMemo\` | Note count + UI placement only |
| `vault.dat.bak` | `%APPDATA%\SecureMemo\` | Previous vault backup |

## Project layout

```
src/
  app/       Application, NoteManager, SecurityGuard
  crypto/    Auth, AES-GCM, VaultStorage, PublicIndex
  model/     Note serialization
  ui/        List, Note windows, Lock dialog, Tray
  util/      Secure buffer wipe
installer/   Inno Setup script
resources/   Icon + version resource
```

## Security notes

- Data at rest is encrypted; unlock only keeps plaintext in memory while unlocked.
- Force-killing the process cannot show a password prompt (OS limitation); disk data remains encrypted.
- Choose a strong master password.

## License

[MIT](LICENSE)
