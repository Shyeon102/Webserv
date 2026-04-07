# Webserv
Webserv project in École 42 paris.  
Seonghyeon Kim  
Jihye Kim  
Jaeone Oh  

# GitHub 협업 가이드

## 1) 기본 규칙
- `main`은 **항상 빌드/실행 가능한 상태**를 유지한다.
- 모든 작업은 **브랜치에서 진행**하며, `main`에 직접 작업하지 않는다.
- `main` 반영은 **Pull Request(PR)로만** 진행한다. (`main` 직접 push 금지)

---

## 2) 브랜치 전략
- `main`  
  - 제출/배포 기준 브랜치  
  - 항상 안정 상태 유지

- `feature/<이름>-<작업>`  
  - 기능 개발 브랜치  
  - 예)
    - `feature/ksh-eventLoop`
    - `feature/kjh-httpParser`
    - `feature/jaoh-cgiBasic`

- `fix/<이슈>`  
  - 버그 수정 브랜치  
  - 예)
    - `fix/partialRead`
    - `fix/makefile-build`

**규칙**
- 모든 브랜치는 `main`에서 분기한다.
- 작업 완료 후 PR을 통해 `main`에 머지한다.

---

## 3) 이슈 규칙
- 작업 시작 전 **GitHub Issue 1개 생성** (작업 단위 기준)
- 이슈에는 아래 내용을 포함한다:
  - 작업 내용 체크리스트 (2~5개)
  - 완료 기준

**이슈 예시**
- 제목: `HTTP header parser 구현`
- 내용:
  - [ ] request line 파싱
  - [ ] header key/value 파싱
  - [ ] Content-Length 숫자 파싱
  - [ ] 기본 에러 케이스 처리

(선택)
- 브랜치명에 이슈 번호 포함 가능  
  - 예: `feature/B-12-httpParser`

---

## 4) 커밋 규칙 (예시 포함)
커밋 메시지는 아래 접두어를 사용한다.

- `feat:` 기능 추가
- `fix:` 버그 수정
- `refactor:` 리팩토링 (동작 변경 최소)
- `docs:` 문서 수정
- `test:` 테스트 추가/수정

**커밋 메시지 예시**
- `feat: implement HttpRequest header parsing`
- `feat: add Content-Length extraction`
- `fix: handle partial read in Connection`
- `refactor: split Server accept/read logic`
- `docs: add github collaboration guide`
- `test: add parser edge case tests`

**추가 규칙**
- 하나의 커밋에는 하나의 목적만 담는다.
- 커밋 전 `make`가 정상 동작해야 한다.

---

## 5) PR 규칙

### PR 제목
- 형식: `[파트] 작업 요약`
- 예)
  - `[A] non-blocking read/write buffer`
  - `[B] HttpRequest header parser`
  - `[C] CGI basic execution`

### PR 본문에 포함할 내용
- **What**: 무엇을 했는지 요약
- **Test**: 테스트 방법
- **Affected**: 영향 범위(수정한 파일/모듈)

**본문 예시**
- What:
  - HttpRequest에서 request line + header 파싱 구현
  - Content-Length 파싱 함수 추가
- Test:
  - `make && ./webserv config/default.conf`
  - curl로 GET/POST 테스트
- Affected:
  - `HttpRequest.*`
  - `Parser.*`

### 머지 조건
- 최소 **1명 승인(Approve)** 필요
- 충돌 발생 시 **PR 작성자가 해결**
- 머지 전 필수 조건:
  - `make` 성공
  - 기본 동작 테스트 완료

---

## 6) 코드 스타일 / 포맷
- 클래스 / 타입 이름: **PascalCase**
  - 예: `HttpRequest`, `HttpResponse`, `EventLoop`
- 함수 / 변수 이름: **camelCase**
  - 예: `parseHeaders()`, `readFromSocket()`, `contentLength`

---

## 7) 충돌 방지 룰
- 공용 파일 또는 핵심 구조(Server, Connection, 공용 헤더 등)를 크게 수정할 경우:
  1. **카카오톡으로 사전 공지**
  2. 변경 내용을 **GitHub Issue에 기록**
- 동시에 동일 파일을 대규모 수정하는 상황을 최대한 피한다.

---

## 8) 동기화 루틴

### 작업 시작 시
```bash
git checkout main
git pull origin main
```
### 작업 브랜치에 main 반영
```bash
git checkout feature/...
git merge main
```
### PR 전 체크
```bash
make
```

---

## 9) 금지 사항

- main 브랜치에 직접 push 금지
- Makefile이 정상 동작하지 않으면 push/PR 금지
