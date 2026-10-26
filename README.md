# CopyCoach

재즈 기타 일일 루틴(지판 위 4모듈) + 곡 카피 보조(스템 분리·감속·녹음·A/B·악보)를 한 앱에서 돌리는 Windows 데스크톱 프로그램. C++23 · Qt 6 · CMake · vcpkg.

- 설계 문서는 이 저장소 밖(개발 폴더 상위)에 있다. 빌드 입력 데이터만 `data/`에 둔다
- 빌드: `tools\dev-build.bat dev-debug`(VS 2026 Build Tools + CLion 번들 CMake/Ninja) 또는 CLion에서 `CMakePresets.json` 열기
- 서브모듈: `vcpkg`(패키지), `third_party/verovio`(악보 조판, 6.3.0) — `git submodule update --init --depth 1`
- 자체 코드 라이선스 MIT(`LICENSE`). 공개 바이너리는 GPLv3 구성 요소를 포함하므로 GPLv3 조건으로 배포(`PLAN.md` §3)
