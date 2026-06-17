$ErrorActionPreference = "Stop"

$root = Split-Path -Parent (Split-Path -Parent $MyInvocation.MyCommand.Path)
$exe = Join-Path $root "build\hash-forge.exe"
$tableTests = Join-Path $root "build\hash_table_tests.exe"
$arrayTests = Join-Path $root "build\dynamic_array_tests.exe"
$summaryPath = Join-Path $root "out\summary.txt"
$reportPath = Join-Path $root "out\report.md"
$benchPath = Join-Path $root "out\bench.md"
$comparePath = Join-Path $root "out\compare.md"
$baselinesPath = Join-Path $root "out\baselines.md"
$historyMdPath = Join-Path $root "out\history.md"
$bestCPath = Join-Path $root "out\best.c"
$bestTxtPath = Join-Path $root "out\best.txt"
$latestReportPath = Join-Path $root "out\latest_report_path.txt"
$latestExportPath = Join-Path $root "out\latest_export_path.txt"
$historyPath = Join-Path $root "out\history.csv"

function Fail($message) {
    throw "TEST FAILED: $message"
}

function Assert($condition, $message) {
    if (-not $condition) {
        Fail $message
    }
}

function Join-ProcessArguments([string[]]$arguments) {
    $quoted = foreach ($argument in $arguments) {
        if ($argument -match '[\s"]') {
            '"' + ($argument -replace '"', '\"') + '"'
        } else {
            $argument
        }
    }
    $quoted -join " "
}

function Invoke-Captured($file, [string[]]$arguments, [int[]]$allowedExitCodes = @(0)) {
    $psi = New-Object System.Diagnostics.ProcessStartInfo
    $psi.FileName = $file
    $psi.Arguments = Join-ProcessArguments $arguments
    $psi.UseShellExecute = $false
    $psi.RedirectStandardOutput = $true
    $psi.RedirectStandardError = $true
    $process = New-Object System.Diagnostics.Process
    $process.StartInfo = $psi
    [void]$process.Start()
    $stdout = $process.StandardOutput.ReadToEnd()
    $stderr = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    $exitCode = $process.ExitCode
    $text = ($stdout + $stderr).TrimEnd()
    if ($allowedExitCodes -notcontains $exitCode) {
        Fail "command failed: $file $($arguments -join ' ')`nexit=$exitCode`n$text"
    }
    [pscustomobject]@{
        ExitCode = $exitCode
        Output = $text
    }
}

function Read-RunSummary {
    Assert (Test-Path $summaryPath) "missing out\summary.txt"
    $line = Get-Content $summaryPath | Select-Object -Last 1
    $pattern = 'id=(\d+) generation=(\d+) run_generation=(\d+) quick=(-?\d+) deep=(-?\d+) flags=0x([0-9a-fA-F]+) elapsed_seconds=([0-9.]+) stop_reason=(.*?) quality=(\w+) threads=(\d+) quick_candidates=(\d+) deep_candidates=(\d+) total_candidates=(\d+)'
    $match = [regex]::Match($line, $pattern)
    Assert $match.Success "summary line did not match expected format: $line"
    $starterMatch = [regex]::Match($line, 'starter_candidates=(\d+)')
    $refreshMatch = [regex]::Match($line, 'refresh_enabled=(yes|no)')
    [pscustomobject]@{
        Id = $match.Groups[1].Value
        CandidateGeneration = [uint64]$match.Groups[2].Value
        RunGeneration = [uint64]$match.Groups[3].Value
        Quick = [int64]$match.Groups[4].Value
        Deep = [int64]$match.Groups[5].Value
        Flags = $match.Groups[6].Value
        ElapsedSeconds = [double]$match.Groups[7].Value
        StopReason = $match.Groups[8].Value
        Quality = $match.Groups[9].Value
        Threads = [uint32]$match.Groups[10].Value
        QuickCandidates = [uint64]$match.Groups[11].Value
        DeepCandidates = [uint64]$match.Groups[12].Value
        TotalCandidates = [uint64]$match.Groups[13].Value
        StarterCandidates = if ($starterMatch.Success) { [uint32]$starterMatch.Groups[1].Value } else { 8 }
        RefreshEnabled = if ($refreshMatch.Success) { $refreshMatch.Groups[1].Value } else { "yes" }
    }
}

function Assert-ReportContains($summary) {
    Assert (Test-Path $reportPath) "missing out\report.md"
    $report = Get-Content $reportPath -Raw
    Assert ($report -match [regex]::Escape("- Stop reason: ``$($summary.StopReason)``")) "report missing stop reason"
    Assert ($report -match [regex]::Escape("- Quality: ``$($summary.Quality)``")) "report missing quality"
    Assert ($report -match [regex]::Escape("- Scoring threads: ``$($summary.Threads)``")) "report missing thread count"
    Assert ($report -match [regex]::Escape("- Crossover children per generation: ``16``")) "report missing crossover count"
    Assert ($report -match [regex]::Escape("- Random immigrants per generation: ``8``")) "report missing immigrant count"
    Assert ($report -match [regex]::Escape("- Compact starter candidates: ``$($summary.StarterCandidates)``")) "report missing starter count"
    Assert ($report -match [regex]::Escape("- Total hash functions evaluated: ``$($summary.TotalCandidates)``")) "report missing total evaluated"
    Assert ($report -match "Diversity telemetry") "report missing diversity telemetry"
    Assert ($report -match "Unique candidates in last scored generation") "report missing unique candidate count"
    Assert ($report -match "Duplicate candidate repairs") "report missing duplicate repair count"
    Assert ($report -match "Stagnation refreshes") "report missing stagnation refresh count"
    Assert ($report -match "Extra adaptive random immigrants") "report missing adaptive immigrant count"
    $hexId = "{0:x}" -f ([uint64]$summary.Id)
    Assert ($report -match [regex]::Escape("- ID: ``$hexId``")) "report missing best id"
    Assert ($report -match "Quick score") "report missing quick score"
    Assert ($report -match "Final deep score") "report missing deep score"
    Assert ($report -match "Fail flags") "report missing fail flags"
    Assert ($report -match "Score breakdown") "report missing score breakdown"
    Assert ($report -match "\| differentials \|") "report missing differential breakdown row"
    Assert ($report -match "\| sensitivity \|") "report missing sensitivity breakdown row"
    Assert ($report -match "\| total \|") "report missing score breakdown total row"
    Assert ($report -match "Final multi-seed audit") "report missing final multi-seed audit"
    Assert ($report -match "Worst audit deep score") "report missing audit worst score"
    Assert ($report -match "Combined audit flags") "report missing audit combined flags"
    Assert ($report -match "Operator histogram") "report missing operator histogram"
    Assert ($report -match "\| XOR \|") "report missing operator histogram rows"
    Assert ($report -match "Baseline comparison") "report missing baseline comparison"
    Assert ($report -match "baseline_mixer") "report missing baseline mixer comparison"
    Assert ($report -match "bad_xor_only") "report missing bad xor comparison"
    Assert ($report -match "Established hash baselines") "report missing established baselines"
    Assert ($report -match "splitmix64_finalizer") "report missing splitmix baseline"
    Assert ($report -match "murmur3_fmix64") "report missing murmur fmix baseline"
    Assert ($report -match "fnv1a64_pair") "report missing fnv baseline"
    Assert ($report -match [regex]::Escape("out/runs/*.md")) "report missing archived report reference"
    Assert ($report -match [regex]::Escape("out/runs/*.c")) "report missing archived C export reference"
    Assert ($report -match [regex]::Escape("out/history.csv")) "report missing history CSV reference"
    Assert ($report -match [regex]::Escape("out/baselines.md")) "report missing baselines report reference"
    Assert ($report -match "VM instruction listing") "report missing instruction listing"
    Assert ($report -match [regex]::Escape("out/best.c")) "report missing best.c reference"
    Assert (Test-Path $latestReportPath) "missing out\latest_report_path.txt"
    $archiveRelative = (Get-Content $latestReportPath | Select-Object -First 1).Trim()
    Assert ($archiveRelative -match '^out/runs/.+\.md$') "latest report path has unexpected shape: $archiveRelative"
    $archiveFull = Join-Path $root $archiveRelative
    Assert (Test-Path $archiveFull) "latest archived report does not exist: $archiveRelative"
    Assert (Test-Path $latestExportPath) "missing out\latest_export_path.txt"
    $exportRelative = (Get-Content $latestExportPath | Select-Object -First 1).Trim()
    Assert ($exportRelative -match '^out/runs/.+\.c$') "latest export path has unexpected shape: $exportRelative"
    $exportFull = Join-Path $root $exportRelative
    Assert (Test-Path $exportFull) "latest archived C export does not exist: $exportRelative"
    Assert (Test-Path $historyPath) "missing out\history.csv"
    $history = Get-Content $historyPath
    Assert ($history[0] -match "unix_time,seed,run_generation") "history CSV missing expected header"
    Assert ($history.Count -ge 2) "history CSV missing run rows"
}

function Invoke-RunAndReadSummary([string[]]$arguments) {
    $result = Invoke-Captured $exe $arguments
    Assert ($result.Output -match "Run complete") "run output missing completion section"
    Assert ($result.Output -match "total evaluated") "run output missing total evaluated"
    $summary = Read-RunSummary
    Assert-ReportContains $summary
    Assert (Test-Path $bestCPath) "missing out\best.c"
    Assert (Test-Path $bestTxtPath) "missing out\best.txt"
    $bestTxt = Get-Content $bestTxtPath -Raw
    Assert ($bestTxt -match "exported_instruction_count") "best.txt missing exported instruction count"
    $summary
}

function Compile-BestC {
    Assert (Test-Path $bestCPath) "missing out\best.c before compile"
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    Assert (Test-Path $vswhere) "missing vswhere.exe"
    $vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    Assert $vsPath "Visual Studio C++ Build Tools were not found"
    $devCmd = Join-Path $vsPath "Common7\Tools\VsDevCmd.bat"
    Assert (Test-Path $devCmd) "missing VsDevCmd.bat"
    $objPath = Join-Path $root "build\best.obj"
    $cmd = "`"$devCmd`" -arch=x64 >nul && cl /nologo /TC /std:c11 /O2 /c `"$bestCPath`" /Fo:`"$objPath`""
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        Fail "out\best.c did not compile"
    }
    $checkExe = Join-Path $root "build\best_check.exe"
    $cmd = "`"$devCmd`" -arch=x64 >nul && cl /nologo /TC /std:c11 /O2 /DHASH_FORGE_BEST_TEST_MAIN `"$bestCPath`" /Fe:`"$checkExe`""
    cmd.exe /c $cmd
    if ($LASTEXITCODE -ne 0) {
        Fail "out\best.c test executable did not compile"
    }
    $result = Invoke-Captured $checkExe @()
    Assert ($result.Output -match "vectors: pass") "out\best.c vector test did not pass"
}

function Assert-BenchReport([string[]]$expectedThreadTexts) {
    Assert (Test-Path $benchPath) "missing out\bench.md"
    $bench = Get-Content $benchPath -Raw
    Assert ($bench -match "hash-forge benchmark report") "bench report missing title"
    Assert ($bench -match "Quality") "bench report missing quality setting"
    Assert ($bench -match "quick/sec") "bench report missing quick/sec"
    Assert ($bench -match "deep/sec") "bench report missing deep/sec"
    Assert ($bench -match "quick hash/sec") "bench report missing quick hash/sec"
    Assert ($bench -match "deep hash/sec") "bench report missing deep hash/sec"
    Assert ($bench -match "hash evals per candidate") "bench report missing hash eval count"
    Assert ($bench -match "Best quick throughput") "bench report missing best quick interpretation"
    Assert ($bench -match "Best deep throughput") "bench report missing best deep interpretation"
    Assert ($bench -match "Best quick hash throughput") "bench report missing best quick hash interpretation"
    foreach ($text in $expectedThreadTexts) {
        Assert ($bench -match [regex]::Escape($text)) "bench report missing row fragment: $text"
    }
}

function Assert-CompareReport {
    Assert (Test-Path $comparePath) "missing out\compare.md"
    $compare = Get-Content $comparePath -Raw
    Assert ($compare -match "hash-forge policy comparison") "compare report missing title"
    Assert ($compare -match "Generations per trial") "compare report missing generation setting"
    Assert ($compare -match "Policy summary") "compare report missing policy summary"
    Assert ($compare -match "avg deep") "compare report missing aggregate deep score"
    Assert ($compare -match "wins") "compare report missing win counts"
    Assert ($compare -match "Trial results") "compare report missing trial results"
    Assert ($compare -match "default") "compare report missing default policy"
    Assert ($compare -match "no-starter") "compare report missing no-starter policy"
    Assert ($compare -match "no-refresh") "compare report missing no-refresh policy"
    Assert ($compare -match "bare") "compare report missing bare policy"
}

function Assert-BaselinesReport {
    Assert (Test-Path $baselinesPath) "missing out\baselines.md"
    $baselines = Get-Content $baselinesPath -Raw
    Assert ($baselines -match "hash-forge established hash baselines") "baselines report missing title"
    Assert ($baselines -match "Seed") "baselines report missing seed"
    Assert ($baselines -match "Quality") "baselines report missing quality"
    Assert ($baselines -match "splitmix64_finalizer") "baselines report missing splitmix"
    Assert ($baselines -match "murmur3_fmix64") "baselines report missing murmur fmix"
    Assert ($baselines -match "fnv1a64_pair") "baselines report missing fnv"
    Assert ($baselines -match "flags") "baselines report missing flags"
    Assert ($baselines -match "Hash evals per reference") "baselines report missing eval count"
    Assert ($baselines -match "How to use this") "baselines report missing interpretation"
}

function Assert-HistoryReport {
    Assert (Test-Path $historyMdPath) "missing out\history.md"
    $historyReport = Get-Content $historyMdPath -Raw
    Assert ($historyReport -match "hash-forge history") "history report missing title"
    Assert ($historyReport -match "Top rows shown") "history report missing top count"
    Assert ($historyReport -match "best id") "history report missing best id column"
    Assert ($historyReport -match "deep") "history report missing deep score column"
    Assert ($historyReport -match "total candidates") "history report missing total candidates column"
    Assert ($historyReport -match "starters") "history report missing starter policy column"
    Assert ($historyReport -match "refresh") "history report missing refresh policy column"
    Assert ($historyReport -match "flag names") "history report missing decoded flag column"
}

$lockRoot = Join-Path $root "build"
New-Item -ItemType Directory -Force -Path $lockRoot | Out-Null
$lockDir = Join-Path $lockRoot "test.lock"
try {
    New-Item -ItemType Directory -Path $lockDir -ErrorAction Stop | Out-Null
} catch {
    Fail "another hash-forge test run is already active"
}

try {
Set-Location $root
& (Join-Path $root "build.ps1")

$selfTest = Invoke-Captured $exe @("self-test")
Assert ($selfTest.Output -match "compact_starter score=.*flags=0x") "self-test missing compact starter calibration"
Assert ($selfTest.Output -match "bad_key_only score=.*flags=0x6f") "self-test no longer flags bad_key_only sensitivity failure"
Assert ($selfTest.Output -match "bad_seed_only score=.*flags=0x6f") "self-test no longer flags bad_seed_only sensitivity failure"
Assert ($selfTest.Output -match "bad_xor_only score=.*flags=0x2b") "self-test no longer flags bad_xor_only differential failure"
Invoke-Captured $tableTests @() | Out-Null
Invoke-Captured $arrayTests @() | Out-Null

$missingSeed = Invoke-Captured $exe @("run") @(2)
Assert ($missingSeed.Output -match "run requires --seed") "missing-seed error text changed"
$badSeconds = Invoke-Captured $exe @("run", "--seed", "123", "--seconds", "0") @(2)
Assert ($badSeconds.Output -match "invalid --seconds") "zero-seconds error text changed"
$badThreads = Invoke-Captured $exe @("run", "--seed", "123", "--threads", "0") @(2)
Assert ($badThreads.Output -match "invalid --threads") "zero-threads error text changed"
$badQuality = Invoke-Captured $exe @("run", "--seed", "123", "--quality", "maximum") @(2)
Assert ($badQuality.Output -match "invalid --quality") "bad-quality error text changed"
$badBench = Invoke-Captured $exe @("bench") @(2)
Assert ($badBench.Output -match "bench requires --seconds") "missing bench seconds error text changed"
$badCompare = Invoke-Captured $exe @("compare", "--seeds", "0") @(2)
Assert ($badCompare.Output -match "invalid --seeds") "bad-compare-seeds error text changed"
$badBaselines = Invoke-Captured $exe @("baselines", "--quality", "maximum") @(2)
Assert ($badBaselines.Output -match "invalid --quality") "bad-baselines-quality error text changed"

$thread1 = Invoke-RunAndReadSummary @("run", "--seed", "123", "--generations", "100", "--threads", "1")
Assert ($thread1.RunGeneration -eq 100) "thread=1 run did not complete 100 generations"
Assert ($thread1.StopReason -eq "generation limit") "thread=1 run stop reason was not generation limit"
Assert ($thread1.Threads -eq 1) "thread=1 summary reported $($thread1.Threads)"

$thread4 = Invoke-RunAndReadSummary @("run", "--seed", "123", "--generations", "100", "--threads", "4")
Assert ($thread4.RunGeneration -eq 100) "thread=4 run did not complete 100 generations"
Assert ($thread4.StopReason -eq "generation limit") "thread=4 run stop reason was not generation limit"
Assert ($thread4.Threads -eq 4) "thread=4 summary reported $($thread4.Threads)"
Assert ($thread1.Id -eq $thread4.Id) "thread=1 and thread=4 best ids differ"
Assert ($thread1.Quick -eq $thread4.Quick) "thread=1 and thread=4 quick scores differ"
Assert ($thread1.Deep -eq $thread4.Deep) "thread=1 and thread=4 deep scores differ"
Assert ($thread1.TotalCandidates -eq $thread4.TotalCandidates) "thread=1 and thread=4 total evaluations differ"

foreach ($threads in @(2, 4, 999)) {
    $summary = Invoke-RunAndReadSummary @("run", "--seed", "456", "--generations", "5", "--threads", "$threads")
    $expectedThreads = if ($threads -eq 999) { 32 } else { $threads }
    Assert ($summary.Threads -eq $expectedThreads) "thread stress $threads reported $($summary.Threads)"
    Assert ($summary.RunGeneration -eq 5) "thread stress $threads did not complete 5 generations"
    Assert ($summary.TotalCandidates -gt 0) "thread stress $threads had zero total candidates"
}

$defaultThreadSummary = Invoke-RunAndReadSummary @("run", "--seed", "456", "--generations", "5")
Assert ($defaultThreadSummary.Threads -ge 1) "default thread count below 1"
Assert ($defaultThreadSummary.Threads -le 32) "default thread count above cap"
Assert ($defaultThreadSummary.RunGeneration -eq 5) "default-thread run did not complete 5 generations"

$autoThreadSummary = Invoke-RunAndReadSummary @("run", "--seed", "456", "--generations", "5", "--threads", "auto")
Assert ($autoThreadSummary.Threads -ge 1) "auto thread count below 1"
Assert ($autoThreadSummary.Threads -le 32) "auto thread count above cap"
Assert ($autoThreadSummary.RunGeneration -eq 5) "auto-thread run did not complete 5 generations"

$deepQualitySummary = Invoke-RunAndReadSummary @("run", "--seed", "789", "--generations", "5", "--threads", "2", "--quality", "deep")
Assert ($deepQualitySummary.Quality -eq "deep") "deep quality run reported $($deepQualitySummary.Quality)"
Assert ($deepQualitySummary.RunGeneration -eq 5) "deep quality run did not complete 5 generations"
Assert ($deepQualitySummary.DeepCandidates -ge 17) "deep quality run did not deep-score expected candidates"

$toggleSummary = Invoke-RunAndReadSummary @("run", "--seed", "321", "--generations", "5", "--threads", "2", "--no-starter", "--no-refresh")
Assert ($toggleSummary.RunGeneration -eq 5) "toggle run did not complete 5 generations"
$toggleReport = Get-Content $reportPath -Raw
Assert ($toggleReport -match [regex]::Escape("- Compact starter candidates: ``0``")) "toggle report did not disable starter lane"
Assert ($toggleReport -match [regex]::Escape("- Stagnation refresh window: ``0`` generations")) "toggle report did not disable refresh"

$timeSummary = Invoke-RunAndReadSummary @("run", "--seed", "123", "--seconds", "1", "--threads", "4")
Assert ($timeSummary.StopReason -eq "time limit") "time-limited run did not stop by time"
Assert ($timeSummary.Threads -eq 4) "time-limited run reported wrong thread count"
Assert ($timeSummary.TotalCandidates -gt 0) "time-limited run had zero total candidates"

$benchResult = Invoke-Captured $exe @("bench", "--seed", "123", "--seconds", "1", "--threads", "1,2,999", "--quality", "quick")
Assert ($benchResult.Output -match "Benchmark complete") "bench output missing completion section"
Assert ($benchResult.Output -match "quality") "bench output missing quality"
Assert ($benchResult.Output -match "quick/sec") "bench output missing quick/sec"
Assert ($benchResult.Output -match "deep/sec") "bench output missing deep/sec"
Assert ($benchResult.Output -match "qhash/sec") "bench output missing qhash/sec"
Assert ($benchResult.Output -match "dhash/sec") "bench output missing dhash/sec"
Assert-BenchReport @("| 1 | 1 |", "| 2 | 2 |", "| 32 | 32 |")

$compareResult = Invoke-Captured $exe @("compare", "--seed", "123", "--seeds", "1", "--generations", "3", "--threads", "1", "--quality", "quick")
Assert ($compareResult.Output -match "Compare complete") "compare output missing completion"
Assert ($compareResult.Output -match "best policy") "compare output missing best policy"
Assert-CompareReport

$baselinesResult = Invoke-Captured $exe @("baselines", "--seed", "123", "--quality", "quick", "--quick")
Assert ($baselinesResult.Output -match "Baselines complete") "baselines output missing completion"
Assert ($baselinesResult.Output -match "splitmix64_finalizer") "baselines output missing splitmix"
Assert ($baselinesResult.Output -match "murmur3_fmix64") "baselines output missing murmur fmix"
Assert ($baselinesResult.Output -match "fnv1a64_pair") "baselines output missing fnv"
Assert-BaselinesReport

$badHistory = Invoke-Captured $exe @("history", "--top", "0") @(2)
Assert ($badHistory.Output -match "invalid --top") "bad-history-top error text changed"
$historyResult = Invoke-Captured $exe @("history", "--top", "3")
Assert ($historyResult.Output -match "Hash Forge history top runs") "history output missing title"
Assert ($historyResult.Output -match "History complete") "history output missing completion"
Assert-HistoryReport

Compile-BestC

Write-Host "hash-forge strict tests: pass"
} finally {
    Remove-Item -LiteralPath $lockDir -Recurse -Force -ErrorAction SilentlyContinue
}
