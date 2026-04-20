const state = {
   user: null,
   currentClassId: null,
   currentTaskId: null,
   currentTaskTitle: "",
   statusFilter: "all",
   selectedSubmissionId: null,
   submissions: [],
   aiReportExpanded: true
};

const dashboardLayout = document.getElementById("app");
const leftPanelResizeHandle = document.getElementById("leftPanelResizeHandle");
const rightPanelResizeHandle = document.getElementById("rightPanelResizeHandle");
const leftPanelWidthStorageKey = "aichecker.leftPanelWidth";
const rightPanelWidthStorageKey = "aichecker.rightPanelWidth";

const userBox = document.getElementById("userBox");
const classesList = document.getElementById("classesList");
const tasksList = document.getElementById("tasksList");
const tasksTitle = document.getElementById("tasksTitle");
const reviewTitle = document.getElementById("reviewTitle");
const submissionsTableBody = document.querySelector("#submissionsTable tbody");
const logBox = document.getElementById("logBox");
const checkProgress = document.getElementById("checkProgress");

const pullBtn = document.getElementById("pullBtn");
const openTaskClassroomBtn = document.getElementById("openTaskClassroomBtn");
const runChecksAllBtn = document.getElementById("runChecksAllBtn");
const editSitesBtn = document.getElementById("editSitesBtn");
const uploadDatasetBtn = document.getElementById("uploadDatasetBtn");
const sendGradesBtn = document.getElementById("sendGradesBtn");
const sendCommentsBtn = document.getElementById("sendCommentsBtn");
const exportZipBtn = document.getElementById("exportZipBtn");
const datasetUploadInput = document.getElementById("datasetUploadInput");

const pullOneBtn = document.getElementById("pullOneBtn");
const sendCommentOneBtn = document.getElementById("sendCommentOneBtn");
const exportOneBtn = document.getElementById("exportOneBtn");
const runChecksOneBtn = document.getElementById("runChecksOneBtn");
const saveNotesBtn = document.getElementById("saveNotesBtn");
const openClassroomBtn = document.getElementById("openClassroomBtn");

const selectedStudentCard = document.getElementById("selectedStudentCard");
const teacherCommentInput = document.getElementById("teacherCommentInput");
const aiReportBox = document.getElementById("aiReportBox");
const aiReportToggleBtn = document.getElementById("aiReportToggleBtn");
const statusFilterMenu = document.getElementById("statusFilterMenu");
const checkingSubmissionIds = new Set();

function readStoredPanelWidth(storageKey) {
   try {
      const raw = window.localStorage.getItem(storageKey);
      if (!raw) {
         return null;
      }

      const parsed = Number.parseInt(raw, 10);
      return Number.isNaN(parsed) ? null : parsed;
   } catch {
      return null;
   }
}

function saveStoredPanelWidth(storageKey, width) {
   try {
      window.localStorage.setItem(storageKey, String(Math.round(width)));
   } catch {
   }
}

function setCheckProgress(text, visible) {
   if (!checkProgress) {
      return;
   }

   checkProgress.textContent = text || "";
   checkProgress.classList.toggle("hidden", !visible);
}

function log(msg) {
   if (!logBox) {
      setCheckProgress(msg, true);
      return;
   }

   logBox.textContent += msg + "\n";
   logBox.scrollTop = logBox.scrollHeight;
}

async function apiGet(path) {
   const res = await fetch(path, { credentials: "include" });
   return await res.json();
}

async function apiPost(path, body) {
   const res = await fetch(path, {
      method: "POST",
      credentials: "include",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {})
   });

   return await res.json();
}

function splitSiteList(raw) {
   const text = String(raw || "");
   const parts = text.split(/[\n,;\t\r]+/g);
   const out = [];

   for (const part of parts) {
      const value = part.trim();
      if (value.length > 0) {
         out.push(value);
      }
   }

   return out;
}

function asList(items) {
   if (!Array.isArray(items) || items.length === 0) {
      return "- немає";
   }

   return items.map((item) => "- " + item).join("\n");
}

function firstFailedRow(rows) {
   if (!Array.isArray(rows)) {
      return null;
   }

   for (const row of rows) {
      if (row && row.ok === false) {
         return row;
      }
   }

   return null;
}

function buildGradeFailureMessage(row, fallbackMessage) {
   if (row && row.reason === "project_permission_denied") {
      const manualUrl = row.manualUrl ? " Відкрити завдання в Classroom: " + row.manualUrl : "";
      return "Google Classroom API блокує зміну оцінки для цього завдання (ProjectPermissionDenied). Потрібно ставити оцінку вручну в Classroom або використовувати coursework, створений цим самим Google Cloud проектом." + manualUrl;
   }

   if (row && row.message) {
      return row.message;
   }

   return fallbackMessage || "Не вдалося надіслати оцінку";
}

function splitTaskId(taskId) {
   const raw = String(taskId || "");
   const marker = "__";
   const index = raw.indexOf(marker);

   if (index < 0) {
      return null;
   }

   const courseId = raw.slice(0, index);
   const courseWorkId = raw.slice(index + marker.length);

   if (!courseId || !courseWorkId) {
      return null;
   }

   return { courseId, courseWorkId };
}

function encodeClassroomPathId(rawId) {
   const text = String(rawId || "");
   if (!text) {
      return "";
   }

   try {
      const encoded = window.btoa(text);
      return encoded.replace(/=+$/g, "").replace(/\+/g, "-").replace(/\//g, "_");
   } catch {
      return "";
   }
}

function buildClassroomTeacherUrl(taskId) {
   const parsed = splitTaskId(taskId);

   if (!parsed) {
      return "";
   }

   const encodedCourseId = encodeClassroomPathId(parsed.courseId);
   const encodedCourseWorkId = encodeClassroomPathId(parsed.courseWorkId);

   if (!encodedCourseId || !encodedCourseWorkId) {
      return "";
   }

   return "https://classroom.google.com/c/" + encodedCourseId + "/a/" + encodedCourseWorkId + "/submissions/by-status/and-sort-last-name/all/all";
}

function buildClassroomStudentUrl(taskId, submission) {
   const baseUrl = buildClassroomTeacherUrl(taskId);

   const directLink = submission && submission.classroomStudentLink
      ? String(submission.classroomStudentLink).trim()
      : "";

   if (directLink) {
      return directLink;
   }

   // Classroom web expects a student filter token derived from submission id.
   const submissionId = submission && submission.id ? submission.id : "";
   const studentFilterTokenRaw = String(submissionId || "") + "Z";
   const encodedStudentId = encodeClassroomPathId(studentFilterTokenRaw);

   if (!baseUrl || !encodedStudentId) {
      return "";
   }

   return baseUrl.replace("/all/all", "/student/" + encodedStudentId);
}

function openUrlReliable(url) {
   const normalized = String(url || "").trim();

   if (!normalized) {
      return false;
   }

   const popup = window.open(normalized, "_blank", "noopener,noreferrer");

   if (popup && !popup.closed) {
      popup.opener = null;
      return true;
   }

   return false;
}

function applySectionToggleState(button, body, expanded) {
   if (!button || !body) {
      return;
   }

   const arrow = button.querySelector(".section-toggle-arrow");
   button.setAttribute("aria-expanded", expanded ? "true" : "false");
   body.classList.toggle("hidden", !expanded);

   if (arrow) {
      arrow.textContent = expanded ? "▾" : "▸";
   }
}

function renderToggleSections() {
   applySectionToggleState(aiReportToggleBtn, aiReportBox, state.aiReportExpanded);
}

function formatAiReportForDisplay(rawReport) {
   if (!rawReport || String(rawReport).trim().length === 0) {
      return "-";
   }

   try {
      const json = JSON.parse(rawReport);

      if (!json || typeof json !== "object") {
         return String(rawReport);
      }

      const score = Number(json.score || 0);
      const verdict = String(json.verdict || "");
      const thinking = String(json.thinking || "");
      const indicators = asList(json.indicators);
      const strong = asList(json.strong_parts);
      const issues = asList(json.issues);
      const recommendations = asList(json.recommendations);
      const emailComment = String(json.email_comment || "");

      return [
         "Ймовірність AI: " + score.toFixed(1) + "%",
         "Вердикт: " + verdict,
         "",
         "Коротке пояснення:",
         thinking || "-",
         "",
         "Сильні сторони:",
         strong,
         "",
         "Індикатори:",
         indicators,
         "",
         "Проблеми:",
         issues,
         "",
         "Рекомендації:",
         recommendations,
         "",
         "Коментар для email:",
         emailComment || "-"
      ].join("\n");
   } catch {
      return String(rawReport);
   }
}

function selectedSubmission() {
   if (!state.selectedSubmissionId) {
      return null;
   }

   return state.submissions.find((s) => s.id === state.selectedSubmissionId) || null;
}

function workflowStatus(sub) {
   if (sub.sent) {
      return "sended";
   }
   if (sub.approved) {
      return "checked";
   }
   if (sub.status === "missing") {
      return "missing";
   }
   if (sub.late) {
      return "turned_in_late";
   }
   if (sub.status === "turned_in") {
      return "turned_in";
   }
   return "missing";
}

function workflowLabel(status) {
   const labels = {
      missing: "Відсутня",
      turned_in_late: "Здано із запізненням",
      turned_in: "Здано",
      checked: "Перевірено",
      sended: "Надіслано"
   };
   return labels[status] || status;
}

function renderUser() {
   if (!state.user) {
      userBox.textContent = "";
      return;
   }

   userBox.innerHTML = "<b>" + state.user.name + "</b><br><small>" + state.user.email + "</small><br><button id='logoutBtn'>Вийти</button>";

   document.getElementById("logoutBtn").onclick = async function() {
      await apiPost("/api/auth/logout", {});
      window.location.href = "/login.html";
   };
}

function renderSelectedStudentPanel() {
   const sub = selectedSubmission();

   if (!sub) {
      selectedStudentCard.textContent = "Виберіть студента зі списку.";
      teacherCommentInput.value = "";
      aiReportBox.textContent = "-";
      return;
   }

   const status = workflowStatus(sub);
   selectedStudentCard.textContent =
      "Студент: " + sub.studentName + "\n" +
      "Електронна пошта: " + sub.studentEmail + "\n" +
      "Репозиторій: " + (sub.repositoryUrl || "немає") + "\n" +
      "Статус: " + workflowLabel(status);

   teacherCommentInput.value = sub.teacherComment || sub.feedback || "";
   aiReportBox.textContent = formatAiReportForDisplay(sub.aiReport);
}

function renderStatusFilterMenu() {
   const buttons = statusFilterMenu.querySelectorAll(".filter-btn");
   buttons.forEach((button) => {
      const value = button.getAttribute("data-filter");
      button.classList.toggle("active", value === state.statusFilter);
   });
}

function filteredSubmissions() {
   if (state.statusFilter === "all") {
      return state.submissions;
   }

   return state.submissions.filter((sub) => workflowStatus(sub) === state.statusFilter);
}

function renderSubmissions() {
   submissionsTableBody.innerHTML = "";

   for (const sub of filteredSubmissions()) {
      const tr = document.createElement("tr");
      if (sub.id === state.selectedSubmissionId) {
         tr.classList.add("active-row");
      }

      tr.onclick = function() {
         state.selectedSubmissionId = sub.id;
         renderSubmissions();
         renderSelectedStudentPanel();
      };

      const student = document.createElement("td");
      student.textContent = sub.studentName;

      const ai = document.createElement("td");
      if (checkingSubmissionIds.has(sub.id)) {
         ai.classList.add("checking-cell");
         ai.textContent = "...";
      } else {
         ai.textContent = Number(sub.aiScore || 0).toFixed(1);
      }

      const pl = document.createElement("td");
      if (checkingSubmissionIds.has(sub.id)) {
         pl.classList.add("checking-cell");
         pl.textContent = "...";
      } else {
         pl.textContent = Number(sub.plagiarismScore || 0).toFixed(1);
      }

      const status = document.createElement("td");
      const statusValue = workflowStatus(sub);
      status.innerHTML = "<span class='status-pill status-" + statusValue + "'>" + workflowLabel(statusValue) + "</span>";

      tr.appendChild(student);
      tr.appendChild(ai);
      tr.appendChild(pl);
      tr.appendChild(status);
      submissionsTableBody.appendChild(tr);
   }
}

async function loadMe() {
   const me = await apiGet("/api/auth/me");
   if (!me.ok) {
      window.location.href = "/login.html";
      return false;
   }

   state.user = me.user;
   renderUser();
   log("Вхід успішний: " + state.user.name);
   return true;
}

async function loadClasses() {
   const data = await apiGet("/api/classes");
   if (data.ok === false) {
      log(data.message || "Не вдалося завантажити класи");
      return;
   }

   classesList.innerHTML = "";
   for (const c of data.classes) {
      const button = document.createElement("button");
      button.textContent = c.name;
      button.onclick = async function() {
         state.currentClassId = c.id;
         await loadTasks(c.id, c.name);
      };
      classesList.appendChild(button);
   }
}

async function loadTasks(classId, className) {
   tasksTitle.textContent = "Завдання: " + className;
   const data = await apiGet("/api/classes/" + classId + "/tasks");

   if (data.ok === false) {
      log(data.message || "Не вдалося завантажити завдання");
      return;
   }

   tasksList.innerHTML = "";
   for (const t of data.tasks) {
      const stats = await apiGet("/api/tasks/" + t.id + "/stats");
      const button = document.createElement("button");
      button.textContent = t.title + " | Відсутні: " + (stats.missing || 0) + " | Запізнення: " + (stats.turnedInLate || 0);
      button.onclick = async function() {
         state.currentTaskId = t.id;
         state.currentTaskTitle = t.title;
         state.selectedSubmissionId = null;
         await reloadSubmissions();
      };
      tasksList.appendChild(button);
   }
}

async function reloadSubmissions() {
   if (!state.currentTaskId) {
      return;
   }

   reviewTitle.textContent = "Список студентів: " + state.currentTaskTitle;
   const data = await apiGet("/api/tasks/" + state.currentTaskId + "/submissions");

   if (data.ok === false) {
      log(data.message || "Не вдалося завантажити роботи");
      return;
   }

   state.submissions = data.submissions || [];
   if (state.selectedSubmissionId && !state.submissions.some((s) => s.id === state.selectedSubmissionId)) {
      state.selectedSubmissionId = null;
   }
   if (!state.selectedSubmissionId && state.submissions.length > 0) {
      state.selectedSubmissionId = state.submissions[0].id;
   }

   renderStatusFilterMenu();
   renderSubmissions();
   renderSelectedStudentPanel();
}

async function requireTaskAndSelection(requireSelection) {
   if (!state.currentTaskId) {
      log("Спочатку оберіть завдання");
      return null;
   }

   if (requireSelection) {
      const sub = selectedSubmission();
      if (!sub) {
         log("Оберіть студента у таблиці");
         return null;
      }
      return sub;
   }

   return {};
}

pullBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/pull-repos", {});
   const total = data.rows && Array.isArray(data.rows) ? data.rows.length : 0;
   const success = data.rows && Array.isArray(data.rows) ? data.rows.filter((row) => row && row.ok).length : 0;
   const skipped = data.rows && Array.isArray(data.rows) ? data.rows.filter((row) => row && row.skipped).length : 0;
   log("Отримано репозиторії: всього=" + total + ", успішно=" + success + ", пропущено=" + skipped);
};

editSitesBtn.onclick = async function() {
   try {
      const current = await apiGet("/api/review/plagiarism/sites");
      if (!current.ok) {
         log(current.message || "Не вдалося отримати whitelist/blacklist");
         return;
      }

      const whitelistInput = window.prompt(
         "Whitelist сайтів (через кому або новий рядок):",
         Array.isArray(current.whitelist) ? current.whitelist.join("\n") : ""
      );

      if (whitelistInput === null) {
         return;
      }

      const blacklistInput = window.prompt(
         "Blacklist сайтів (через кому або новий рядок):",
         Array.isArray(current.blacklist) ? current.blacklist.join("\n") : ""
      );

      if (blacklistInput === null) {
         return;
      }

      const saved = await apiPost("/api/review/plagiarism/sites", {
         whitelist: splitSiteList(whitelistInput),
         blacklist: splitSiteList(blacklistInput)
      });

      if (!saved.ok) {
         log(saved.message || "Не вдалося зберегти whitelist/blacklist");
         return;
      }

      const whiteCount = Array.isArray(saved.whitelist) ? saved.whitelist.length : 0;
      const blackCount = Array.isArray(saved.blacklist) ? saved.blacklist.length : 0;
      log("Оновлено списки сайтів: whitelist=" + whiteCount + ", blacklist=" + blackCount);
   } catch (e) {
      log("Помилка оновлення списків сайтів");
   }
};

uploadDatasetBtn.onclick = function() {
   if (!datasetUploadInput) {
      log("Поле завантаження dataset не знайдено");
      return;
   }

   datasetUploadInput.value = "";
   datasetUploadInput.click();
};

if (datasetUploadInput) {
   datasetUploadInput.onchange = async function() {
      const file = datasetUploadInput.files && datasetUploadInput.files[0];

      if (!file) {
         return;
      }

      try {
         const text = await file.text();
         const data = await apiPost("/api/review/plagiarism/dataset/upload", {
            datasetText: text
         });

         if (!data.ok) {
            log(data.message || "Не вдалося завантажити dataset");
            return;
         }

         log("Dataset оновлено з файлу: " + file.name + " | рядків=" + Number(data.rows || 0));
      } catch (e) {
         log("Помилка читання/завантаження dataset файлу");
      }
   };
}

runChecksAllBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   if (!state.submissions || state.submissions.length === 0) {
      await reloadSubmissions();
   }

   const targets = [...state.submissions];

   if (targets.length === 0) {
      setCheckProgress("Немає студентів для перевірки.", true);
      return;
   }

   runChecksAllBtn.disabled = true;
   let processed = 0;
   setCheckProgress("Перевірка AI/PL: 0/" + targets.length, true);

   for (const sub of targets) {
      checkingSubmissionIds.add(sub.id);
      renderSubmissions();

      try {
         const ai = await apiPost("/api/review/ai", { submissionId: sub.id, taskId: state.currentTaskId });
         const pl = await apiPost("/api/review/plagiarism", { submissionId: sub.id, taskId: state.currentTaskId });

         sub.aiScore = Number(ai.aiScore || 0);
         sub.plagiarismScore = Number(pl.plagiarismScore || 0);
      } catch (e) {
         log("Помилка перевірки для " + sub.studentName);
      }

      processed += 1;
      checkingSubmissionIds.delete(sub.id);
      setCheckProgress("Перевірка AI/PL: " + processed + "/" + targets.length, true);
      renderSubmissions();
   }

   runChecksAllBtn.disabled = false;
   setCheckProgress("Перевірку завершено: " + processed + "/" + targets.length, true);
   await reloadSubmissions();
};

if (sendGradesBtn) {
   sendGradesBtn.onclick = async function() {
      const ok = await requireTaskAndSelection(false);
      if (!ok) {
         return;
      }

      const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-grades", {});
      const failed = Number(data.failed || 0);
      const firstFailed = firstFailedRow(data.rows);

      if (!data.ok && failed > 0) {
         log("Не всі оцінки надіслані: помилок=" + failed + " | " + buildGradeFailureMessage(firstFailed, data.message));
         return;
      }

      log("Надіслано оцінки (усім): надіслано=" + Number(data.sent || 0) + ", пропущено=" + Number(data.skipped || 0) + ", помилок=" + failed);
      await reloadSubmissions();
   };
}

sendCommentsBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-comments-email", {
      taskTitle: state.currentTaskTitle
   });
   const failed = Number(data.failed || 0);
   const firstFailed = firstFailedRow(data.rows);

   if (!data.ok && failed > 0) {
      log("Не всі коментарі надіслані: помилок=" + failed + (firstFailed && firstFailed.message ? " | " + firstFailed.message : ""));
      return;
   }

   log("Оброблено коментарі (усім): успішно=" + Number(data.sent || 0) + ", помилок=" + failed);
   await reloadSubmissions();
};

exportZipBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/export-zip", {});
   if (!data.ok) {
      log(data.message || "Не вдалося створити ZIP");
      return;
   }

   log("Експорт ZIP (усі): " + data.zipPath);
   if (data.downloadUrl) {
      window.open(data.downloadUrl, "_blank");
   }
};

pullOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/pull-repos", { submissionId: sub.id });
   const row = data.rows && Array.isArray(data.rows) && data.rows.length > 0 ? data.rows[0] : null;
   if (!row) {
      log("Отримано репозиторій: " + sub.studentName + " -> дані відповіді відсутні");
      return;
   }

   if (row.ok) {
      log("Отримано репозиторій: " + sub.studentName + " -> успішно");
   } else {
      log("Отримано репозиторій: " + sub.studentName + " -> " + (row.message || "помилка"));
   }
};

sendCommentOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const teacherComment = teacherCommentInput.value || "";

   const notes = await apiPost("/api/review/notes", {
      submissionId: sub.id,
      taskId: state.currentTaskId,
      teacherComment
   });

   if (!notes.ok) {
      log(notes.message || "Не вдалося зберегти коментар");
      return;
   }

   const data = await apiPost("/api/review/send-email", {
      submissionId: sub.id,
      taskId: state.currentTaskId,
      taskTitle: state.currentTaskTitle
   });
   if (!data.ok) {
      log("Не вдалося надіслати коментар (обраному): " + (data.message || "помилка"));
      return;
   }

   const message = String(data.message || "");
   if (message.includes("Gmail не надіслав")) {
      log("Коментар не надіслано через Gmail: " + message);
   } else {
      log("Надіслано коментар (обраному): " + (message || "виконано"));
   }
   await reloadSubmissions();
};

exportOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/export-zip", { submissionId: sub.id });
   if (!data.ok) {
      log(data.message || "Не вдалося створити ZIP");
      return;
   }

   log("Експорт ZIP (обраний): " + sub.studentName);
   if (data.downloadUrl) {
      window.open(data.downloadUrl, "_blank");
   }
};

runChecksOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   checkingSubmissionIds.add(sub.id);
   renderSubmissions();
   setCheckProgress("Перевірка AI/PL для: " + sub.studentName, true);

   try {
      const ai = await apiPost("/api/review/ai", { submissionId: sub.id, taskId: state.currentTaskId });
      const pl = await apiPost("/api/review/plagiarism", { submissionId: sub.id, taskId: state.currentTaskId });
      log("Перевірено: " + sub.studentName + " | AI=" + Number(ai.aiScore || 0).toFixed(1) + "% PL=" + Number(pl.plagiarismScore || 0).toFixed(1) + "%");
      await reloadSubmissions();
   } finally {
      checkingSubmissionIds.delete(sub.id);
      renderSubmissions();
   }
};

saveNotesBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const notes = await apiPost("/api/review/notes", {
      submissionId: sub.id,
      taskId: state.currentTaskId,
      teacherComment: teacherCommentInput.value || ""
   });

   if (!notes.ok) {
      log(notes.message || "Не вдалося зберегти нотатки");
      return;
   }

   log("Збережено коментар для: " + sub.studentName);
   await reloadSubmissions();
};

if (openTaskClassroomBtn) {
   openTaskClassroomBtn.onclick = function() {
      if (!state.currentTaskId) {
         log("Спочатку оберіть завдання");
         return;
      }

      const taskUrl = buildClassroomTeacherUrl(state.currentTaskId);

      if (!taskUrl) {
         log("Не вдалося зібрати посилання Classroom для цього завдання");
         return;
      }

      if (!openUrlReliable(taskUrl)) {
         log("Браузер заблокував відкриття нового вікна. Дозвольте pop-up для цього сайту.");
      }
   };
}

if (openClassroomBtn) {
   openClassroomBtn.onclick = function() {
      const sub = selectedSubmission();

      if (!sub) {
         log("Оберіть студента у таблиці");
         return;
      }

      const taskUrl = buildClassroomStudentUrl(state.currentTaskId, sub);
      if (!taskUrl) {
         log("Не вдалося зібрати student-посилання Classroom для обраної роботи.");
         return;
      }

      if (!openUrlReliable(taskUrl)) {
         log("Браузер заблокував відкриття нового вікна. Дозвольте pop-up для цього сайту.");
      }
   };
}

if (aiReportToggleBtn) {
   aiReportToggleBtn.onclick = function() {
      state.aiReportExpanded = !state.aiReportExpanded;
      renderToggleSections();
   };
}

function initLeftPanelResize() {
   if (!dashboardLayout || !leftPanelResizeHandle) {
      return;
   }

   function clampLeftPanelWidth(width) {
      const minWidth = 220;
      const maxWidth = Math.max(minWidth, Math.floor(window.innerWidth * 0.35));
      const numericWidth = Number(width || 0);
      return Math.max(minWidth, Math.min(maxWidth, numericWidth));
   }

   function applyLeftPanelWidth(width) {
      const clamped = clampLeftPanelWidth(width);
      dashboardLayout.style.setProperty("--left-panel-width", Math.round(clamped) + "px");
      saveStoredPanelWidth(leftPanelWidthStorageKey, clamped);
   }

   const restoredLeftWidth = readStoredPanelWidth(leftPanelWidthStorageKey);
   if (restoredLeftWidth !== null && !window.matchMedia("(max-width: 1200px)").matches) {
      applyLeftPanelWidth(restoredLeftWidth);
   }

   let isResizing = false;

   leftPanelResizeHandle.addEventListener("mousedown", function(event) {
      if (window.matchMedia("(max-width: 1200px)").matches) {
         return;
      }

      isResizing = true;
      dashboardLayout.classList.add("resizing");
      event.preventDefault();
   });

   window.addEventListener("mousemove", function(event) {
      if (!isResizing) {
         return;
      }

      const nextWidth = event.clientX - 12;
      applyLeftPanelWidth(nextWidth);
   });

   window.addEventListener("mouseup", function() {
      if (!isResizing) {
         return;
      }

      isResizing = false;
      dashboardLayout.classList.remove("resizing");
   });

   window.addEventListener("resize", function() {
      if (window.matchMedia("(max-width: 1200px)").matches) {
         dashboardLayout.style.removeProperty("--left-panel-width");
         return;
      }

      const current = getComputedStyle(dashboardLayout).getPropertyValue("--left-panel-width");
      const numeric = Number.parseInt(current, 10);

      if (!Number.isNaN(numeric)) {
         applyLeftPanelWidth(numeric);
         return;
      }

      const restored = readStoredPanelWidth(leftPanelWidthStorageKey);
      if (restored !== null) {
         applyLeftPanelWidth(restored);
      }
   });
}

function initRightPanelResize() {
   if (!dashboardLayout || !rightPanelResizeHandle) {
      return;
   }

   function clampRightPanelWidth(width) {
      const minWidth = 300;
      const maxWidth = Math.max(minWidth, Math.floor(window.innerWidth * 0.65));
      const numericWidth = Number(width || 0);
      return Math.max(minWidth, Math.min(maxWidth, numericWidth));
   }

   function applyRightPanelWidth(width) {
      const clamped = clampRightPanelWidth(width);
      dashboardLayout.style.setProperty("--right-panel-width", Math.round(clamped) + "px");
      saveStoredPanelWidth(rightPanelWidthStorageKey, clamped);
   }

    const restoredRightWidth = readStoredPanelWidth(rightPanelWidthStorageKey);
    if (restoredRightWidth !== null && !window.matchMedia("(max-width: 1200px)").matches) {
      applyRightPanelWidth(restoredRightWidth);
   }

   let isResizing = false;

   rightPanelResizeHandle.addEventListener("mousedown", function(event) {
      if (window.matchMedia("(max-width: 1200px)").matches) {
         return;
      }

      isResizing = true;
      dashboardLayout.classList.add("resizing");
      event.preventDefault();
   });

   window.addEventListener("mousemove", function(event) {
      if (!isResizing) {
         return;
      }

      const nextWidth = window.innerWidth - event.clientX - 12;
      applyRightPanelWidth(nextWidth);
   });

   window.addEventListener("mouseup", function() {
      if (!isResizing) {
         return;
      }

      isResizing = false;
      dashboardLayout.classList.remove("resizing");
   });

   window.addEventListener("resize", function() {
      if (window.matchMedia("(max-width: 1200px)").matches) {
         dashboardLayout.style.removeProperty("--right-panel-width");
         return;
      }

      const current = getComputedStyle(dashboardLayout).getPropertyValue("--right-panel-width");
      const numeric = Number.parseInt(current, 10);

      if (!Number.isNaN(numeric)) {
         applyRightPanelWidth(numeric);
         return;
      }

      const restored = readStoredPanelWidth(rightPanelWidthStorageKey);
      if (restored !== null) {
         applyRightPanelWidth(restored);
      }
   });
}

statusFilterMenu.addEventListener("click", async function(event) {
   const target = event.target;
   if (!(target instanceof HTMLElement)) {
      return;
   }

   const filter = target.getAttribute("data-filter");
   if (!filter) {
      return;
   }

   state.statusFilter = filter;
   renderStatusFilterMenu();
   renderSubmissions();
});

(async function bootstrap() {
   const ok = await loadMe();
   if (!ok) {
      return;
   }

   renderToggleSections();
   initLeftPanelResize();
   initRightPanelResize();
   renderStatusFilterMenu();
   await loadClasses();
})();
