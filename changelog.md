# Changelog

## 2026-04-20
Поточне оновлення зосереджене на якості перевірки та зручності для викладача. Додано розширений модуль плагіату з n-грамами та Rabin-Karp, підтримку керованих списків сайтів (whitelist/blacklist), нові дії в інтерфейсі для редагування правил сайтів і завантаження датасету, а також логіку підвищення ризику, якщо в роботі згадуються контрольовані джерела без коректного блоку посилань у кінці тексту.

Також виправлено інтерфейс правої панелі (перенесення довгих рядків у межах блоку), додано збереження результатів перевірки між перезапусками застосунку та оновлено стиль AI-звітів, щоб вони залишались зрозумілими для викладача навіть у резервних сценаріях.

Додатково цього ж дня виправлено змішування результатів між різними завданнями: збережений стан рев'ю тепер ізольовано по парі `taskId + submissionId`, API-ендпоїнти рев'ю приймають `taskId`, фронтенд передає його в запитах, а при перемиканні завдання скидається активний вибір студента. Це прибрало ситуацію, коли при переході на інше завдання могли відображатися попередні відповіді.

Окремо виконано чистку конфігурації під web/server-режим: із [settings/app_settings.cfg](settings/app_settings.cfg), [src/config/AppSettings.h](src/config/AppSettings.h) та [src/config/AppSettings.cpp](src/config/AppSettings.cpp) видалено desktop-орієнтовані параметри (`theme.*`, `ui.*`, `window.*`, `classroom.useApiImport`, `classroom.courseId`, `classroom.courseWorkId`, `classroom.studentGroup`). Після змін збірка залишилася успішною.

Оновлено UX-потік відкриття Google Classroom зі сторінки перевірки. Додано дві окремі дії: верхня кнопка відкриває поточне завдання (список усіх submissions), а кнопка в правій панелі відкриває завдання для обраного студента. Для student-режиму використовується `alternateLink` із Classroom API, що усуває проблему з некоректним `student/...` токеном і нескінченним завантаженням сторінки.

Також спрощено праву панель: прибрано шумний блок Classroom mini, залишено тільки потрібні кнопки відкриття, а з таблиці робіт видалено колонку «Оцінка», оскільки в поточному сценарії оцінки більше не виставляються через інтерфейс.

## 2026-04-13
Коміт 652dcb694a621799229477b5c99e48ffe00f51bc оновив великий пласт серверної, інтеграційної та фронтенд-логіки для стабілізації AI-перевірки. Розширено конфігурацію Ollama, додано окремі шаблони промптів у [settings/ollama/prompts/01_json_contract.txt](settings/ollama/prompts/01_json_contract.txt), [settings/ollama/prompts/02_evidence_rules.txt](settings/ollama/prompts/02_evidence_rules.txt), [settings/ollama/prompts/03_language_quality.txt](settings/ollama/prompts/03_language_quality.txt), а також оновлено модельні налаштування в [settings/ollama/Modelfile.aichecker](settings/ollama/Modelfile.aichecker).

На цьому етапі активно доопрацьовувались серверні модулі в [src/server/Services.cpp](src/server/Services.cpp), [src/server/Controllers.cpp](src/server/Controllers.cpp), [src/server/Stores.cpp](src/server/Stores.cpp), інтеграції Classroom/GitHub/Ollama та UI в [site/dashboard.js](site/dashboard.js), [site/styles.css](site/styles.css), [site/login.js](site/login.js). Зміни були спрямовані на виправлення некоректних AI-результатів і загальну стабілізацію веб-потоку перевірки.

## 2026-03-17
Коміт ac902b7ae25fced062140027295b1755488e9285 додав тестову та документаційну основу проєкту. У [tests/unitTests.cpp](tests/unitTests.cpp) з'явилися модульні тести для ключових сервісів, оновлено збірку в [CMakeLists.txt](CMakeLists.txt), а в репозиторій додано матеріали покриття [coverage.xml](coverage.xml) і [lcov.info](lcov.info).

Окремо внесено UML-діаграми в каталог [uml](uml) для візуального опису архітектури та сценаріїв системи.

## 2026-03-16
Коміт a87b3b7b28cbf37e6a8ba90d9a5b399600738661 впорядкував CI-процес для C/C++ з CMake. Додано окремий workflow [.github/workflows/c-cpp.yml](.github/workflows/c-cpp.yml), що спростило автоматичну перевірку збірки в репозиторії.

## 2026-03-13
Коміт b89cb289ae6c142161d69b4880b8a3e02c90a1cd відзначив перехід від desktop-підходу до web-архітектури з авторизацією через Google. На цьому етапі було вилучено старий UI стек ImGui та додано нову веб-структуру: [site/index.html](site/index.html), [site/login.html](site/login.html), [site/dashboard.html](site/dashboard.html), а також серверне API в [src/server/Api.cpp](src/server/Api.cpp), [src/server/Controllers.cpp](src/server/Controllers.cpp), [src/server/Services.cpp](src/server/Services.cpp), [src/server/Stores.cpp](src/server/Stores.cpp).

Також додано початкову конфігурацію AI-частини в [settings/ollama/Modelfile.aichecker](settings/ollama/Modelfile.aichecker), [settings/ollama/SYSTEM_PROMPT.txt](settings/ollama/SYSTEM_PROMPT.txt) і стартовий датасет плагіату [settings/plagiarism_dataset.txt](settings/plagiarism_dataset.txt).

## 2026-03-06
Коміт 9e9d78302bb18d881aaf2ec3a41b24e2a8a30a78 зафіксував проміжний «soft checkpoint» із суттєвим розширенням внутрішньої архітектури. Додано стратегії аналізу, event bus, workflow-команди, політику залежностей та допоміжні інтеграційні модулі для експорту, фільтрації та токенів у [src/core](src/core), [src/app](src/app), [src/integration](src/integration), [src/config](src/config).

На цьому кроці також були додані архітектурні нотатки й UML-чернетки, які використовувались як орієнтир для подальшого переходу на веб-версію.

## 2026-02-23
Початковий коміт 276e0370430d698c35e593a7a289cdc3df4f929f створив базову C++ структуру проєкту, CMake-збірку і стартові модулі застосунку. На ранньому етапі до репозиторію також був доданий великий підмодульний набір ImGui-файлів разом із desktop-орієнтованим UI-контуром.

Того ж дня коміт 020e74bd9d0d9826476d9a427d5e9b7960fe5b8e прибрав README.md і скоригував [.gitignore](.gitignore), зафіксувавши ранню чистку структури.
