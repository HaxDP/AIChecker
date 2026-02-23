# AIChecker (C++ / Dear ImGui)

Система для перевірки студентських робіт на виявлення плагіату та використання ШІ.

## Що вже реалізовано

- Dear ImGui desktop UI (GLFW + OpenGL3)
- Архітектура без Singleton
- Мінімум 3 патерни:
  - **Strategy**: `IAnalysisStrategy` + 3 аналізатори
  - **Factory**: `StrategyFactory` для складання pipeline
  - **Observer**: `EventBus` + `IObserver` для логів
  - **Command**: кроки pipeline (`LoadStudents`, `Analyze`, `Sync`, ...)
- Integration stubs:
  - Google Classroom grade sync stub
  - Email feedback stub (для Gmail)

## Структура

- `src/core/Models.h` — доменні моделі
- `src/core/Architecture.h` — патерни + workflow команди
- `src/integration/Services.h` — адаптери/шлюзи (поки stubs)
- `src/app/AppController.*` — orchestration use-cases
- `src/ui/MainWindow.*` — повний Dear ImGui інтерфейс
- `src/main.cpp` — запуск вікна та рендер-цикл

## Екрани інтерфейсу

1. **Top Bar**: кроки pipeline кнопками + `Run Full Pipeline`
2. **Assignment Settings**:
   - Google Course ID
   - Google CourseWork ID
   - GitHub Classroom URL
   - Max points
3. **Students Table** (ПІБ, email, github)
4. **Submissions Table**:
   - Student, Repo, Plagiarism %, AI %, Grade, Status, Summary
5. **Event Log**

## Build (Windows)

### Вимоги

- CMake 3.20+
- C++20 compiler (MSVC / clang / gcc)
- OpenGL драйвер

### Команди

```powershell
cmake -S . -B build
cmake --build build --config Release
```

Запуск:

```powershell
.\build\Release\AIChecker.exe
```

(Для single-config генераторів запуск може бути `build\AIChecker.exe`)

## Як довести до продакшн

1. **Google Classroom API**
   - Реалізувати `IClassroomGateway` замість stub
   - Для grade sync використовувати `studentSubmissions.patch`
2. **Gmail API / SMTP**
   - Реалізувати `IEmailGateway` (OAuth2 + send)
3. **GitHub Classroom ingestion**
   - Завантаження roster CSV + repo links
4. **Реальні аналізатори**
   - plagiarism engine
   - AI-likelihood model/service
5. **Безпека**
   - Токени/секрети через env vars або vault, не в коді

## Важливе обмеження

Автоматична **оцінка** в Google Classroom — так.
Автоматичний **приватний коментар на submission** через API — зазвичай обмежений.
Практичний шлях: детальний feedback листом/GitHub, а grade — у Classroom.
