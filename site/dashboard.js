const state = {
   user: null,
   currentClassId: null,
   currentTaskId: null,
   currentTaskTitle: "",
   statusFilter: "all",
   selectedSubmissionId: null,
   submissions: []
};

const userBox = document.getElementById("userBox");
const classesList = document.getElementById("classesList");
const tasksList = document.getElementById("tasksList");
const tasksTitle = document.getElementById("tasksTitle");
const reviewTitle = document.getElementById("reviewTitle");
const submissionsTableBody = document.querySelector("#submissionsTable tbody");
const logBox = document.getElementById("logBox");
const checkProgress = document.getElementById("checkProgress");

const pullBtn = document.getElementById("pullBtn");
const runChecksAllBtn = document.getElementById("runChecksAllBtn");
const sendGradesBtn = document.getElementById("sendGradesBtn");
const sendCommentsBtn = document.getElementById("sendCommentsBtn");
const exportZipBtn = document.getElementById("exportZipBtn");

const pullOneBtn = document.getElementById("pullOneBtn");
const sendGradeOneBtn = document.getElementById("sendGradeOneBtn");
const sendCommentOneBtn = document.getElementById("sendCommentOneBtn");
const exportOneBtn = document.getElementById("exportOneBtn");
const runChecksOneBtn = document.getElementById("runChecksOneBtn");
const saveNotesBtn = document.getElementById("saveNotesBtn");

const selectedStudentCard = document.getElementById("selectedStudentCard");
const gradeInput = document.getElementById("gradeInput");
const allowLateMaxGrade = document.getElementById("allowLateMaxGrade");
const teacherCommentInput = document.getElementById("teacherCommentInput");
const aiReportBox = document.getElementById("aiReportBox");
const statusFilterMenu = document.getElementById("statusFilterMenu");
const checkingSubmissionIds = new Set();

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

function asList(items) {
   if (!Array.isArray(items) || items.length === 0) {
      return "- немає";
   }

   return items.map((item) => "- " + item).join("\n");
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
      gradeInput.value = 4;
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

   gradeInput.value = sub.grade > 0 ? sub.grade : (sub.late ? 4 : 5);
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

      const grade = document.createElement("td");
      grade.textContent = sub.grade > 0 ? String(sub.grade) : "-";

      const status = document.createElement("td");
      const statusValue = workflowStatus(sub);
      status.innerHTML = "<span class='status-pill status-" + statusValue + "'>" + workflowLabel(statusValue) + "</span>";

      tr.appendChild(student);
      tr.appendChild(ai);
      tr.appendChild(pl);
      tr.appendChild(grade);
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
         const ai = await apiPost("/api/review/ai", { submissionId: sub.id });
         const pl = await apiPost("/api/review/plagiarism", { submissionId: sub.id });

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

sendGradesBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-grades", {});
   if (!data.ok) {
      log(data.message || "Не вдалося надіслати оцінки");
      return;
   }

   log("Надіслано оцінки (усім): надіслано=" + data.sent + ", пропущено=" + data.skipped);
   await reloadSubmissions();
};

sendCommentsBtn.onclick = async function() {
   const ok = await requireTaskAndSelection(false);
   if (!ok) {
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-comments-email", {});
   if (!data.ok) {
      log(data.message || "Не вдалося надіслати коментарі");
      return;
   }

   log("Надіслано коментарі (усім): " + data.sent);
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

sendGradeOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const grade = Number(gradeInput.value || 4);
   const teacherComment = teacherCommentInput.value || "";

   const finalize = await apiPost("/api/review/finalize", {
      submissionId: sub.id,
      grade,
      feedback: teacherComment,
      teacherComment,
      allowLateMaxGrade: Boolean(allowLateMaxGrade.checked)
   });

   if (!finalize.submissionId) {
      log(finalize.message || "Не вдалося підтвердити оцінку");
      return;
   }

   const send = await apiPost("/api/tasks/" + state.currentTaskId + "/send-grades", { submissionId: sub.id });
   if (!send.ok) {
      log(send.message || "Не вдалося надіслати оцінку");
      return;
   }

   log("Надіслано оцінку (обраному): " + sub.studentName);
   await reloadSubmissions();
};

sendCommentOneBtn.onclick = async function() {
   const sub = await requireTaskAndSelection(true);
   if (!sub) {
      return;
   }

   const teacherComment = teacherCommentInput.value || "";

   const notes = await apiPost("/api/review/notes", {
      submissionId: sub.id,
      teacherComment
   });

   if (!notes.ok) {
      log(notes.message || "Не вдалося зберегти коментар");
      return;
   }

   const data = await apiPost("/api/review/send-email", { submissionId: sub.id });
   log("Надіслано коментар (обраному): " + (data.message || "виконано"));
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
      const ai = await apiPost("/api/review/ai", { submissionId: sub.id });
      const pl = await apiPost("/api/review/plagiarism", { submissionId: sub.id });
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
      teacherComment: teacherCommentInput.value || ""
   });

   if (!notes.ok) {
      log(notes.message || "Не вдалося зберегти нотатки");
      return;
   }

   log("Збережено коментар для: " + sub.studentName);
   await reloadSubmissions();
};

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

   renderStatusFilterMenu();
   await loadClasses();
})();