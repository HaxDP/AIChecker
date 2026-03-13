const state =
{
   user: null,
   currentClassId: null,
   currentTaskId: null,
   submissions: []
};

const dashboardSection = document.getElementById("dashboardSection");
const tasksSection = document.getElementById("tasksSection");
const reviewSection = document.getElementById("reviewSection");
const userBox = document.getElementById("userBox");
const classesList = document.getElementById("classesList");
const tasksList = document.getElementById("tasksList");
const tasksTitle = document.getElementById("tasksTitle");
const reviewTitle = document.getElementById("reviewTitle");
const submissionsTableBody = document.querySelector("#submissionsTable tbody");
const logBox = document.getElementById("logBox");
const pullBtn = document.getElementById("pullBtn");
const sendGradesBtn = document.getElementById("sendGradesBtn");
const sendCommentsBtn = document.getElementById("sendCommentsBtn");
const exportZipBtn = document.getElementById("exportZipBtn");

function log(msg)
{
   logBox.textContent += msg + "\n";
   logBox.scrollTop = logBox.scrollHeight;
}

async function apiGet(path)
{
   const res = await fetch(path, { credentials: "include" });
   return await res.json();
}

async function apiPost(path, body)
{
   const res = await fetch(path,
   {
      method: "POST",
      credentials: "include",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify(body || {})
   });

   return await res.json();
}

function renderUser()
{
   if (!state.user)
   {
      userBox.textContent = "";
      return;
   }

   userBox.innerHTML = "<b>" + state.user.name + "</b> <button id='logoutBtn'>Вийти</button>";

   document.getElementById("logoutBtn").onclick = async function()
   {
      await apiPost("/api/auth/logout", {});
      window.location.href = "/login.html";
   };
}

async function loadMe()
{
   const me = await apiGet("/api/auth/me");

   if (!me.ok)
   {
      window.location.href = "/login.html";
      return false;
   }

   state.user = me.user;
   renderUser();
   log("Вхід успішний: " + state.user.name);
   return true;
}

async function loadClasses()
{
   const data = await apiGet("/api/classes");

   if (data.ok === false)
   {
      log(data.message || "Не вдалося завантажити класи");
      return;
   }

   classesList.innerHTML = "";

   for (const c of data.classes)
   {
      const btn = document.createElement("button");
      btn.textContent = c.name;
      btn.onclick = async function()
      {
         state.currentClassId = c.id;
         await loadTasks(c.id, c.name);
      };
      classesList.appendChild(btn);
   }
}

async function loadTasks(classId, className)
{
   tasksSection.classList.remove("hidden");
   tasksTitle.textContent = "Завдання класу: " + className;
   const data = await apiGet("/api/classes/" + classId + "/tasks");

   if (data.ok === false)
   {
      log(data.message || "Не вдалося завантажити завдання");
      return;
   }

   tasksList.innerHTML = "";

   for (const t of data.tasks)
   {
      const wrap = document.createElement("div");
      const stats = await apiGet("/api/tasks/" + t.id + "/stats");

      if (stats.ok === false)
      {
         log(stats.message || "Не вдалося завантажити статистику");
         continue;
      }

      const btn = document.createElement("button");
      btn.textContent = t.title + " | В роботі: " + stats.working + ", Завершено: " + stats.completed + ", На рев'ю: " + stats.inReview;
      btn.onclick = async function()
      {
         state.currentTaskId = t.id;
         await loadSubmissions(t.id, t.title);
      };
      wrap.appendChild(btn);
      tasksList.appendChild(wrap);
   }
}

function submissionActionsCell(sub)
{
   const box = document.createElement("div");
   box.className = "action-buttons";

   const aiBtn = document.createElement("button");
   aiBtn.textContent = "AI";
   aiBtn.onclick = async function()
   {
      const model = "aichecker-llama3.2-3b";
      const data = await apiPost("/api/review/ai", { submissionId: sub.id, model });
      log("AI " + sub.studentName + ": " + data.aiScore + "%");
      await reloadSubmissions();
   };

   const plBtn = document.createElement("button");
   plBtn.textContent = "Плагіат";
   plBtn.onclick = async function()
   {
      const data = await apiPost("/api/review/plagiarism", { submissionId: sub.id });
      log("Плагіат " + sub.studentName + ": " + data.plagiarismScore + "%");
      await reloadSubmissions();
   };

   const finalizeBtn = document.createElement("button");
   finalizeBtn.textContent = "Підтвердити";
   finalizeBtn.onclick = async function()
   {
      const grade = Number(prompt("Оцінка (2..5)", sub.grade || 4));
      const feedback = prompt("Фідбек для студента", sub.feedback || "Робота перевірена");
      const data = await apiPost("/api/review/finalize", { submissionId: sub.id, grade, feedback });
      log("Підтверджено: " + data.submissionId);
      await reloadSubmissions();
   };

   const sendBtn = document.createElement("button");
   sendBtn.textContent = "Надіслати email";
   sendBtn.onclick = async function()
   {
      const data = await apiPost("/api/review/send-email", { submissionId: sub.id });
      log(data.message || "Надсилання завершено");
      await reloadSubmissions();
   };

   box.appendChild(aiBtn);
   box.appendChild(plBtn);
   box.appendChild(finalizeBtn);
   box.appendChild(sendBtn);
   return box;
}

async function loadSubmissions(taskId, taskTitle)
{
   reviewSection.classList.remove("hidden");
   reviewTitle.textContent = "Перевірка робіт: " + taskTitle;

   const data = await apiGet("/api/tasks/" + taskId + "/submissions");

   if (data.ok === false)
   {
      log(data.message || "Не вдалося завантажити роботи");
      return;
   }

   state.submissions = data.submissions;
   renderSubmissions();
}

async function reloadSubmissions()
{
   if (!state.currentTaskId)
   {
      return;
   }

   const data = await apiGet("/api/tasks/" + state.currentTaskId + "/submissions");

      if (data.ok === false)
      {
         log(data.message || "Не вдалося оновити роботи");
         return;
      }

   state.submissions = data.submissions;
   renderSubmissions();
}

function renderSubmissions()
{
   submissionsTableBody.innerHTML = "";

   for (const sub of state.submissions)
   {
      const tr = document.createElement("tr");

      const c1 = document.createElement("td");
      c1.textContent = sub.studentName;

      const c2 = document.createElement("td");
      c2.textContent = sub.repositoryUrl;

      const c3 = document.createElement("td");
      c3.textContent = Number(sub.aiScore || 0).toFixed(1);

      const c4 = document.createElement("td");
      c4.textContent = Number(sub.plagiarismScore || 0).toFixed(1);

      const c5 = document.createElement("td");
      c5.textContent = sub.grade || "-";

      const c6 = document.createElement("td");
      c6.textContent = (sub.approved ? "Підтверджено" : "Чернетка") + (sub.sent ? " | Лист відправлено" : "");

      const c7 = document.createElement("td");
      c7.appendChild(submissionActionsCell(sub));

      tr.appendChild(c1);
      tr.appendChild(c2);
      tr.appendChild(c3);
      tr.appendChild(c4);
      tr.appendChild(c5);
      tr.appendChild(c6);
      tr.appendChild(c7);
      submissionsTableBody.appendChild(tr);
   }
}

pullBtn.onclick = async function()
{
   if (!state.currentTaskId)
   {
      log("Спочатку оберіть завдання");
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/pull-repos", {});
   log("Підтягнуто репозиторії: " + data.rows.length);
};

sendGradesBtn.onclick = async function()
{
   if (!state.currentTaskId)
   {
      log("Спочатку оберіть завдання");
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-grades", {});

   if (!data.ok)
   {
      log(data.message || "Не вдалося надіслати оцінки");
      return;
   }

   log("Оцінки відправлено: " + data.sent + ", пропущено: " + data.skipped);
};

sendCommentsBtn.onclick = async function()
{
   if (!state.currentTaskId)
   {
      log("Спочатку оберіть завдання");
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/send-comments-email", {});

   if (!data.ok)
   {
      log(data.message || "Не вдалося надіслати коментарі");
      return;
   }

   log("Коментарі на email відправлено/підготовлено: " + data.sent);
   await reloadSubmissions();
};

exportZipBtn.onclick = async function()
{
   if (!state.currentTaskId)
   {
      log("Спочатку оберіть завдання");
      return;
   }

   const data = await apiPost("/api/tasks/" + state.currentTaskId + "/export-zip", {});

   if (!data.ok)
   {
      log(data.message || "Не вдалося створити ZIP");
      return;
   }

   log("ZIP створено: " + data.zipPath);
   if (data.downloadUrl)
   {
      window.open(data.downloadUrl, "_blank");
   }
};

(async function bootstrap()
{
   const ok = await loadMe();

   if (!ok)
   {
      return;
   }

   await loadClasses();
})();