param(
    [Parameter(Mandatory = $true)]
    [string]$GitHubTokenPath,

    [Parameter(Mandatory = $true)]
    [string]$ClassroomTokenPath,

    [Parameter(Mandatory = $true)]
    [string]$CourseId,

    [Parameter(Mandatory = $false)]
    [string]$CourseWorkId,

    [Parameter(Mandatory = $false)]
    [string]$OllamaModel = "llama3.2:3b"
)

$ErrorActionPreference = "Stop"

function Read-Token([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Token file not found: $path"
    }

    $token = (Get-Content -LiteralPath $path -Raw).Trim()
    if ([string]::IsNullOrWhiteSpace($token)) {
        throw "Token file is empty: $path"
    }

    return $token
}

function Print-Result([string]$name, [bool]$ok, [string]$message) {
    $status = if ($ok) { "PASS" } else { "FAIL" }
    Write-Host "[$status] $name - $message"
}

$allPassed = $true

try {
    $githubToken = Read-Token -path $GitHubTokenPath
    $githubHeaders = @{
        Authorization = "Bearer $githubToken"
        Accept = "application/vnd.github+json"
        "User-Agent" = "AIChecker-Preflight"
        "X-GitHub-Api-Version" = "2022-11-28"
    }

    $ghUser = Invoke-RestMethod -Method Get -Uri "https://api.github.com/user" -Headers $githubHeaders
    Print-Result -name "GitHub token" -ok $true -message "Authenticated as $($ghUser.login)"
}
catch {
    $allPassed = $false
    Print-Result -name "GitHub token" -ok $false -message $_.Exception.Message
}

try {
    $ollamaTags = Invoke-RestMethod -Method Get -Uri "http://localhost:11434/api/tags"
    $hasModel = $false
    if ($ollamaTags.models) {
        $hasModel = ($ollamaTags.models | Where-Object { $_.name -eq $OllamaModel }).Count -gt 0
    }

    if ($hasModel) {
        Print-Result -name "Ollama model" -ok $true -message "Model '$OllamaModel' is installed"
    }
    else {
        $allPassed = $false
        Print-Result -name "Ollama model" -ok $false -message "Model '$OllamaModel' not found. Run: ollama pull $OllamaModel"
    }
}
catch {
    $allPassed = $false
    Print-Result -name "Ollama service" -ok $false -message $_.Exception.Message
}

try {
    $classroomToken = Read-Token -path $ClassroomTokenPath
    $classHeaders = @{
        Authorization = "Bearer $classroomToken"
        Accept = "application/json"
    }

    $courseUri = "https://classroom.googleapis.com/v1/courses/$CourseId"
    $course = Invoke-RestMethod -Method Get -Uri $courseUri -Headers $classHeaders
    Print-Result -name "Classroom course" -ok $true -message "Resolved course '$($course.name)'"

    $studentsUri = "https://classroom.googleapis.com/v1/courses/$CourseId/students?pageSize=10"
    $students = Invoke-RestMethod -Method Get -Uri $studentsUri -Headers $classHeaders
    $count = if ($students.students) { $students.students.Count } else { 0 }
    Print-Result -name "Classroom students" -ok $true -message "Sample fetch count=$count"

    if (-not [string]::IsNullOrWhiteSpace($CourseWorkId)) {
        $workUri = "https://classroom.googleapis.com/v1/courses/$CourseId/courseWork/$CourseWorkId"
        $work = Invoke-RestMethod -Method Get -Uri $workUri -Headers $classHeaders
        Print-Result -name "Classroom task" -ok $true -message "Resolved coursework '$($work.title)'"
    }
}
catch {
    $allPassed = $false
    Print-Result -name "Classroom API" -ok $false -message $_.Exception.Message
}

if ($allPassed) {
    Write-Host "`nAll integration checks passed."
    exit 0
}

Write-Host "`nOne or more checks failed."
exit 1
