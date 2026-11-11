param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    throw "Firmware root folder not found: $Root"
}

if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path (Get-Location) "CD3313_JNI_Candidates"
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$candidateDir = Join-Path $OutputDir "candidates"
New-Item -ItemType Directory -Force -Path $candidateDir | Out-Null

$needles = @(
    "SendCmdAudioMux",
    "ExtAudioMuxer",
    "nativeInit",
    "Java_android_sourceservice_ExtAudioMuxer",
    "/dev/audiomux",
    "audiomux"
)

$report = New-Object System.Collections.Generic.List[object]
$files = @(Get-ChildItem -LiteralPath $Root -Recurse -File -Filter "*.so" -ErrorAction SilentlyContinue)
Write-Host ("Scanning {0} shared libraries under {1}" -f $files.Count, $Root)

foreach ($file in $files) {
    $matches = New-Object System.Collections.Generic.List[string]
    try {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        foreach ($needle in $needles) {
            if ($ascii.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $matches.Add($needle)
            }
        }
        if ($file.Name -ieq "libandroid_servers.so" -and $matches.Count -eq 0) {
            $matches.Add("filename:libandroid_servers.so")
        }
        if ($matches.Count -gt 0) {
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $destName = $hash.Substring(0, 12) + "_" + $file.Name
            $dest = Join-Path $candidateDir $destName
            Copy-Item -LiteralPath $file.FullName -Destination $dest -Force
            $relative = $file.FullName.Substring([System.IO.Path]::GetFullPath($Root).TrimEnd('\').Length).TrimStart('\')
            $report.Add([pscustomobject]@{
                RelativePath = $relative
                SizeBytes = $file.Length
                SHA256 = $hash
                Matches = ($matches -join "; ")
                CopiedAs = $destName
            })
            Write-Host ("MATCH: {0} [{1}]" -f $relative, ($matches -join ", "))
        }
    } catch {
        $report.Add([pscustomobject]@{
            RelativePath = $file.FullName
            SizeBytes = $file.Length
            SHA256 = ""
            Matches = "SCAN_ERROR: $($_.Exception.Message)"
            CopiedAs = ""
        })
    }
}

$reportPath = Join-Path $OutputDir "cd3313_jni_candidates.csv"
$report | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding UTF8
$summaryPath = Join-Path $OutputDir "README.txt"
@(
    "CD3313 JNI candidate scan",
    "Root: $Root",
    "Libraries scanned: $($files.Count)",
    "Matches: $($report | Where-Object { $_.CopiedAs } | Measure-Object | Select-Object -ExpandProperty Count)",
    "Search strings: $($needles -join ', ')",
    "The scanner only reads source files and copies candidate .so files to this output folder."
) | Out-File -LiteralPath $summaryPath -Encoding utf8

$zipPath = "$OutputDir.zip"
Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $zipPath -Force
Write-Host "Candidate archive: $zipPath"
