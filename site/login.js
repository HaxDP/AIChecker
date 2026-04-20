const loginBtn = document.getElementById("googleLoginBtn");
const loginLog = document.getElementById("loginLog");

function log(msg)
{
   if (loginLog)
   {
      loginLog.textContent += msg + "\n";
   }
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

async function tryAutoRedirect()
{
   const me = await apiGet("/api/auth/me");

   if (me.ok)
   {
      window.location.href = "/dashboard.html";
   }
}

async function loginWithGooglePopup()
{
   const cfg = await apiGet("/api/auth/google/config");

   if (!cfg.enabled)
   {
      log("Google OAuth не налаштовано на сервері.");
      return;
   }

   if (!window.google || !window.google.accounts || !window.google.accounts.oauth2)
   {
      log("Google SDK не завантажився. Перевір інтернет/блокувальник.");
      return;
   }

   const client = window.google.accounts.oauth2.initTokenClient(
   {
      client_id: cfg.clientId,
      scope: "openid email profile https://www.googleapis.com/auth/classroom.courses.readonly https://www.googleapis.com/auth/classroom.rosters.readonly https://www.googleapis.com/auth/classroom.coursework.students https://www.googleapis.com/auth/classroom.profile.emails https://www.googleapis.com/auth/gmail.send",
      include_granted_scopes: true,
      callback: async function(tokenResponse)
      {
         if (!tokenResponse || !tokenResponse.access_token)
         {
            log("Google не повернув access token.");
            return;
         }

         const result = await apiPost("/api/auth/google/exchange", { accessToken: tokenResponse.access_token });

         if (!result.ok)
         {
            log(result.message || "Помилка входу через Google.");
            return;
         }

         window.location.href = "/dashboard.html";
      }
   });

   // Force consent once so newly added scopes (like gmail.send) are granted.
   client.requestAccessToken({ prompt: "consent select_account" });
}

loginBtn.onclick = async function()
{
   if (loginLog)
   {
      loginLog.textContent = "";
   }
   log("Відкриваємо Google вікно авторизації...");
   await loginWithGooglePopup();
};

(async function bootstrap()
{
   await tryAutoRedirect();
})();