# TestViewer — 에이전트 작업 안내

Win32 + Direct2D + WIC 기반 Windows 이미지 뷰어. C++20, MSVC v143, x64 전용, 정적 CRT(`/MT`).
기능/단축키 설명은 `README.md`, 설계 배경은 `docs/image-viewer-proposal.md`(길다 — 필요한 절만 읽을 것).

## 컨텍스트 절약 규칙 (가장 중요)

- **로그 파일을 통째로 읽지 않는다.** `build*.log`, `diag.log`(1MB 이상)는 `Select-String`으로 필요한 줄만 찾는다.
- **빌드 출력을 화면으로 받지 않는다.** 아래 빌드 명령처럼 로그 파일에 쓰고 에러 줄만 본다.
- **MSBuild 출력을 `| Select-Object -First N`으로 직접 자르지 않는다.** MSBuild가 중단되면서 `cl.exe`가 남아 명령이 끝나지 않는다. 파일로 저장한 뒤 그 파일을 검색한다.
- 파일은 필요한 함수/구간만 읽는다. 위치는 먼저 `Select-String`(또는 grep 도구)으로 찾는다.
- `out/`, `vcpkg_installed/`, `.git/` 안은 탐색하지 않는다.
- 요청받은 범위만 수정한다. 관련 없는 리팩터링, 포맷 변경, 주석 정리는 하지 않는다.

## 멈춤 규칙 (디버깅)

- 같은 문제에 **3번 시도**해도 원인을 못 찾으면 멈추고 사용자에게 보고한다. 방법을 바꿔 가며 계속 시도하지 않는다.
- 보고 내용: 확인한 사실, 버린 가설, 다음에 필요한 것(재현 방법, 도구, 정보). 5줄 이내.
- **분석용 도구나 프로그램을 직접 만들지 않는다** (덤프 파서, 재현용 하네스, 프로브 등). 필요한 도구가 없으면 멈추고 설치를 요청한다.
- 크래시 덤프(`%LOCALAPPDATA%\CrashDumps\Viewer.exe.*.dmp`)는 `cdb`로만 분석한다. `cdb`가 없으면 덤프를 직접 해석하지 말고 멈춘다.
  ```powershell
  cdb -z "<덤프 경로>" -y out\x64\Debug -c "!analyze -v; q" > dump.log; Select-String -Path dump.log -Pattern 'Viewer!|ExceptionCode|FAULTING|STACK_TEXT' | Select-Object -First 30 | ForEach-Object { $_.Line }
  ```
- GUI 동작에서 생긴 버그는 직접 재현할 수 없다. 재현 절차(연 파일, 누른 키)를 모르면 추측하지 말고 먼저 묻는다.
- 한 세션이 **20스텝**을 넘으면 진행 상황을 `DEBUG_NOTES.md`에 5줄로 적고, 계속할지 사용자에게 묻는다.

## 빌드와 테스트

셸은 Windows PowerShell 5.1이다 (`&&` 사용 불가, `;`로 연결).

```powershell
$msbuild = "C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin\MSBuild.exe"

# Debug 빌드 (로그는 build.log, 화면 출력 없음)
& $msbuild TestViewer.sln /p:Configuration=Debug /p:Platform=x64 /m /nologo /nr:false /noconlog /fl "/flp:logfile=build.log;verbosity=minimal;encoding=utf-8"; "exit=$LASTEXITCODE"

# 실패했을 때만 에러 확인
Select-String -Path build.log -Pattern ' error |FAILED' | Select-Object -First 30 | ForEach-Object { $_.Line }

# 유닛 테스트 (마지막 줄이 "N checks, 0 failures", 실패 시 종료 코드 != 0)
& out\x64\Debug\Tests.exe | Select-Object -Last 15; "exit=$LASTEXITCODE"
```

- `exit=0`이면 빌드 성공이다. 로그를 더 볼 필요 없다.
- Release 빌드는 `Configuration=Release`로 바꾼다. 전체 최적화라 느리므로 요청받았을 때만 한다.
- 산출물: `out\x64\<Debug|Release>\Viewer.exe`, `Tests.exe`, `Core.lib`.
- `Viewer.exe`는 GUI 앱이라 실행하면 창이 뜨고 끝나지 않는다. 검증 목적으로 실행하지 않는다.
- 코드를 수정했으면 Debug 빌드와 `Tests.exe`를 통과시킨 뒤 완료라고 보고한다.

## 구조

```
projects/                Core / Viewer / Tests .vcxproj (솔루션: TestViewer.sln)
src/Core/include/tv/Core.h   Core 공개 API 전체 (네임스페이스 tv) — Core 작업 시 여기부터
src/Core/src/
  DecoderRegistry.cpp    매직 넘버로 디코더 선택, 확장자 목록
  WicDecoder.cpp         JPEG/PNG/GIF/BMP/TIFF/ICO, EXIF 방향 보정
  WebpDecoder.cpp        libwebp
  Decoders.h             디코더 내부 선언
  ImageCache.cpp         바이트 예산 LRU 캐시 (스레드 안전)
  PrefetchScheduler.cpp  백그라운드 선디코딩 (std::jthread)
  FolderList.cpp         폴더 나열 + 자연 정렬
  Settings.cpp           %LOCALAPPDATA%\TestViewer\settings.ini
  FileAssociation.cpp    HKCU 확장자 연결
  Utils.cpp              문자열 변환, 크기/시간 포맷
src/Viewer/
  main.cpp               진입점, 명령줄 인자
  MainWindow.h/.cpp      창 수명, Direct2D 리소스, 입력 처리
  MainWindowUi.cpp       대화상자, 전체화면, 슬라이드쇼, 삭제, 오버레이
  Viewport.h/.cpp        확대/이동/회전/뒤집기 변환
  Thumbnails.h/.cpp      필름스트립 썸네일 (워커 스레드)
src/Tests/main.cpp       CHECK / CHECK_EQ 매크로 기반 콘솔 테스트
```

- 의존 방향: Viewer → Core, Tests → Core. Core는 UI 코드(Direct2D, HWND)를 모른다.
- Core 로직을 바꾸면 `src/Tests/main.cpp`에 테스트를 추가한다. Viewer(UI)는 유닛 테스트가 없다.

## 코드 규칙

- 소스 파일은 **ASCII만, CRLF, BOM 없음**. 주석과 문자열에 한글을 넣지 않는다 (주석은 영어).
- 4칸 들여쓰기, 멤버 변수는 `name_`, 함수는 `camelCase`, 타입은 `PascalCase`.
- 문자열은 유니코드(`std::wstring`, `L"..."`, `W` 계열 Win32 API). 경로는 `std::filesystem::path`.
- 경고 수준 `/W4`. 새 경고를 만들지 않는다.
- COM 포인터는 `Microsoft::WRL::ComPtr`를 쓴다.
- 디코딩 결과는 32bpp BGRA, top-down이며 EXIF 방향이 이미 적용된 상태다 (`tv::DecodedImage`).
- `ImageCache`, `PrefetchScheduler`는 여러 스레드에서 호출된다. 뮤텍스(`mu_`) 규칙을 지킨다.

## 프로젝트 파일

- 소스 파일을 추가/삭제하면 해당 `projects/*.vcxproj`의 `<ClCompile>` / `<ClInclude>` 목록도 수정한다.
- libwebp는 `vcpkg_installed\x64-windows-static`에서 직접 참조한다. 이 폴더를 지우거나 수정하지 않는다.
- 새 외부 라이브러리 추가, `vcpkg.json` 변경, 컴파일 옵션 변경은 먼저 사용자에게 묻는다.

## Git

- 사용자가 요청할 때만 커밋/푸시한다.
- `*.log`는 `.gitignore`에 없다. 로그 파일을 커밋에 포함하지 않는다.
