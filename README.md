# TestViewer

빠른 Windows 이미지 뷰어. Win32 + Direct2D + WIC로 만들어진 경량 뷰어입니다 (Qt 미사용).

- **플랫폼**: Windows 10 22H2+ / Windows 11, x64 전용
- **툴체인**: Visual Studio 2022 (MSVC v143), C++20, MSBuild
- **런타임**: 정적 CRT (`/MT`) — 별도 VC++ 런타임 설치 불필요
- **디코딩**: WIC (JPEG/PNG/GIF/BMP/TIFF/ICO) + libwebp (WebP) — 매직 넘버 기반 자동 판별
- **프로젝트 설계**: `docs/image-viewer-proposal.md` 참조

## 주요 기능 (MVP)

- JPEG / PNG / GIF / BMP / TIFF / ICO / WebP 열기
- 폴더 탐색, Explorer식 자연 정렬 (`StrCmpLogicalW`)
- 휠 확대/축소, 드래그 팬, Fit / 100% 표시
- 회전 / 뒤집기, EXIF 방향 자동 보정
- 전체화면 (F), 슬라이드쇼 (S)
- 백그라운드 선디코딩 (가장 가까운 이미지 우선) + 바이트 예산 기반 LRU 캐시
- 드래그 & 드롭, 명령줄 인자, 파일 확장자 연결 (HKCU, 관리자 불필요)
- 모니터별 DPI v2 지원, 필름스트립 + 정보 오버레이 (I / T)

## 빌드 방법

### 사전 준비
1. Visual Studio 2022 (Community 이상) 설치 — "Desktop development with C++" 워크로드
2. vcpkg 설치 및 `vcpkg integrate install` 실행
   (또는 `VCPKG_ROOT` 환경 변수 설정 — 매니페스트 모드에서 `libwebp` 자동 설치)

### 빌드
```bat
:: Visual Studio에서 TestViewer.sln 열기
:: 또는 명령줄에서:
msbuild TestViewer.sln /p:Configuration=Release /p:Platform=x64
```

실행 파일: `out\x64\Release\Viewer.exe`

### 테스트
```bat
msbuild TestViewer.sln /p:Configuration=Debug /p:Platform=x64
out\x64\Debug\Tests.exe
```

자연 정렬, LRU 캐시, 설정 왕복 직렬화, WIC/WebP 디코더 왕복 테스트를 수행합니다.

## 사용법

```bat
Viewer.exe                 :: 마지막 사용 폴더 열기
Viewer.exe C:\Pictures     :: 폴더 열기
Viewer.exe photo.jpg       :: 파일 열기
```

### 단축키

| 키 | 동작 |
|---|---|
| ← / →, Backspace / Space | 이전 / 다음 이미지 |
| Home / End | 처음 / 마지막 |
| 휠 | 확대 / 축소 |
| 드래그 | 팬 |
| 0 / 1 | Fit / 100% |
| R / E | 시계방향 / 반시계방향 회전 |
| H / V | 수평 / 수평 뒤집기 |
| F | 전체화면 |
| S | 슬라이드쇼 시작/정지 |
| I / T | 정보 오버레이 / 필름스트립 토글 |
| D | 현재 이미지 삭제 (휴지통/영구, 설정 가능) |
| Ctrl+O / Ctrl+F | 파일 / 폴더 열기 |
| Esc | 전체화면 종료 / 닫기 |

## 디렉터리 구조

```
TestViewer.sln
vcpkg.json              # libwebp 매니페스트
assets/                 # 아이콘 + 앱 매니페스트 (DPI v2, longPathAware)
projects/               # Core / Viewer / Tests vcxproj
src/Core/               # 정적 라이브러리: 디코더, 캐시, 선디코딩, 설정
src/Viewer/             # Win32 + Direct2D UI
src/Tests/              # 콘솔 유닛 테스트
docs/                   # 설계 제안서
```
