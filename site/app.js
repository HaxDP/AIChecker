const state =
{
   user: null,
   currentClassId: null,
   currentTaskId: null,
   submissions: []
};

const authSection = document.getElementById("authSection");
const dashboardSection = document.getElementById("dashboardSection");
const tasksSection = document.getElementById("tasksSection");
const reviewSection = document.getElementById("reviewSection");
const userBox = document.getElementById("userBox");
const emailSelect = document.getElementById("emailSelect");
const classesList = document.getElementById("classesList");
const tasksList = document.getElementById("tasksList");
const tasksTitle = document.getElementById("tasksTitle");
const reviewTitle = document.getElementById("reviewTitle");
const submissionsTableBody = document.querySelector("#submissionsTable tbody");
const temperatureInput = document.getElementById("temperatureInput");
const logBox = document.getElementById("logBox");
const pullBtn = document.getElementById("pullBtn");

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

function showAuthMode()
{
   authSection.classList.remove("hidden");
   dashboardSection.classList.add("hidden");
   tasksSection.classList.add("hidden");
   reviewSection.classList.add("hidden");
}

function showAppMode()
{
   authSection.classList.add("hidden");
   dashboardSection.classList.remove("hidden");
}

function renderUser()
{
   if (!state.user)
   {
      userBox.textContent = "";
      return;
   }

   userBox.innerHTML = "<b>" + state.user.name + "</b> (" + state.user.email + ") <button id='logoutBtn'>Вийти</button>";

   document.getElementById("logoutBtn").onclick = async function()
   {
      await apiPost("/api/auth/logout", {});
      state.user = null;
      showAuthMode();
      renderUser();
      log("Вихід виконано");
   };
}

async function loadAccounts()
{
   const data = await apiGet("/api/auth/google/accounts");
   emailSelect.innerHTML = "";

   for (const account of data.accounts)
   {
      const option = document.createElement("option");
      option.value = account.email;
      option.textContent = account.name + " <" + account.email + ">";
      emailSelect.appendChild(option);
   }
}

async function login()
{
   const email = emailSelect.value;
   const data = await apiPost("/api/auth/google/login", { email });

   if (!data.ok)
   {
      log("Помилка входу");
      return;
   }

   await loadMe();
   await loadClasses();
}

async function loadMe()
{
   const me = await apiGet("/api/auth/me");

   if (!me.ok)
   {
      showAuthMode();
      return;
   }

   state.user = me.user;
   renderUser();
   showAppMode();
   log("Вхід успішний: " + state.user.email);
}

async function loadClasses()
{
   const data = await apiGet("/api/classes");
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
   tasksList.innerHTML = "";

   for (const t of data.tasks)
   {
      const wrap = document.createElement("div");
      const stats = await apiGet("/api/tasks/" + t.id + "/stats");
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

   const aiBtn = document.createElement("button");
   aiBtn.textContent = "AI";
   aiBtn.onclick = async function()
   {
      const temperature = Number(temperatureInput.value || 0.1);
      const data = await apiPost("/api/review/ai", { submissionId: sub.id, temperature });
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
   box.appendChild(document.createTextNode(" "));
   box.appendChild(plBtn);
   box.appendChild(document.createTextNode(" "));
   box.appendChild(finalizeBtn);
   box.appendChild(document.createTextNode(" "));
   box.appendChild(sendBtn);
   return box;
}

async function loadSubmissions(taskId, taskTitle)
{
   reviewSection.classList.remove("hidden");
   reviewTitle.textContent = "Перевірка робіт: " + taskTitle;

   const data = await apiGet("/api/tasks/" + taskId + "/submissions");
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
      c1.textContent = sub.studentName + "\n" + sub.studentEmail;

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

document.getElementById("loginBtn").onclick = login;

(async function bootstrap()
{
   await loadAccounts();
   await loadMe();

   if (state.user)
   {
      await loadClasses();
   }
})();