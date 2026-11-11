param(
    [Parameter(Mandatory = $true)]
    [string]$Root,
    [string]$OutputDir = "",
    [long]$MaxFileBytes = 268435456
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
    throw "Firmware root folder not found: $Root"
}

$Root = [System.IO.Path]::GetFullPath($Root)
if ([string]::IsNullOrWhiteSpace($OutputDir)) {
    $OutputDir = Join-Path (Get-Location) "CD3313_Native_Provider"
}
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)
$candidateDir = Join-Path $OutputDir "candidates"
New-Item -ItemType Directory -Force -Path $candidateDir | Out-Null

# Strong markers identify the exact bridge. Generic JNI markers are retained
# only as supporting evidence and never count as an exact CD3313 match alone.
$strongNeedles = @(
    "SendCmdAudioMux",
    "ExtAudioMuxer",
    "Java_android_sourceservice_ExtAudioMuxer",
    "android/sourceservice/ExtAudioMuxer",
    "android.sourceservice.ExtAudioMuxer",
    "/dev/audiomux",
    "audiomux"
)
$supportNeedles = @(
    "nativeInit",
    "JNI_OnLoad",
    "RegisterNatives",
    "registerNativeMethods",
    "sourceinfo",
    "android/sourceservice"
)

$report = New-Object System.Collections.Generic.List[object]
$files = @(Get-ChildItem -LiteralPath $Root -Recurse -File -ErrorAction SilentlyContinue)
$scanned = 0
$skippedLarge = 0
Write-Host ("Inspecting {0} files under {1}" -f $files.Count, $Root)

foreach ($file in $files) {
    if ($file.Length -gt $MaxFileBytes) {
        $skippedLarge++
        continue
    }

    try {
        $bytes = [System.IO.File]::ReadAllBytes($file.FullName)
        $scanned++
        $isElf = $bytes.Length -ge 4 -and
            $bytes[0] -eq 0x7F -and $bytes[1] -eq 0x45 -and
            $bytes[2] -eq 0x4C -and $bytes[3] -eq 0x46

        # ASCII scan is intentional: JNI registration tables and device-node
        # paths are normally stored as NUL-terminated ASCII strings.
        $ascii = [System.Text.Encoding]::ASCII.GetString($bytes)
        $strong = New-Object System.Collections.Generic.List[string]
        $support = New-Object System.Collections.Generic.List[string]

        foreach ($needle in $strongNeedles) {
            if ($ascii.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $strong.Add($needle)
            }
        }
        foreach ($needle in $supportNeedles) {
            if ($ascii.IndexOf($needle, [System.StringComparison]::OrdinalIgnoreCase) -ge 0) {
                $support.Add($needle)
            }
        }

        $knownSystemServer = $file.Name -ieq "libandroid_servers.so"
        $interesting = $strong.Count -gt 0 -or
            ($isElf -and $support.Count -gt 0) -or $knownSystemServer

        if ($interesting) {
            $hash = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
            $relative = $file.FullName.Substring($Root.TrimEnd('\').Length).TrimStart('\')
            $destName = $hash.Substring(0, 12) + "_" + $file.Name
            $dest = Join-Path $candidateDir $destName
            Copy-Item -LiteralPath $file.FullName -Destination $dest -Force

            $report.Add([pscustomobject]@{
                RelativePath = $relative
                SizeBytes = $file.Length
                IsELF = $isElf
                SHA256 = $hash
                StrongMatches = ($strong -join "; ")
                SupportingMatches = ($support -join "; ")
                KnownSystemServerName = $knownSystemServer
                CopiedAs = $destName
            })
            Write-Host ("CANDIDATE: {0} | ELF={1} | strong=[{2}] | support=[{3}]" -f
                $relative, $isElf, ($strong -join ", "), ($support -join ", "))
        }
    } catch {
        $report.Add([pscustomobject]@{
            RelativePath = $file.FullName
            SizeBytes = $file.Length
            IsELF = $false
            SHA256 = ""
            StrongMatches = ""
            SupportingMatches = "SCAN_ERROR: $($_.Exception.Message)"
            KnownSystemServerName = $false
            CopiedAs = ""
        })
    }
}

$reportPath = Join-Path $OutputDir "cd3313_native_candidates.csv"
$report | Export-Csv -LiteralPath $reportPath -NoTypeInformation -Encoding UTF8
$strongCount = @($report | Where-Object { $_.StrongMatches -ne "" }).Count
$elfCount = @($report | Where-Object { $_.IsELF }).Count
$summaryPath = Join-Path $OutputDir "README.txt"
@(
    "CD3313 native-provider candidate scan",
    "Root: $Root",
    "Files discovered: $($files.Count)",
    "Files scanned: $scanned",
    "Files skipped above size limit: $skippedLarge",
    "ELF files among candidates: $elfCount",
    "Candidates with strong CD3313 markers: $strongCount",
    "Max file size: $MaxFileBytes bytes",
    "Strong markers: $($strongNeedles -join ', ')",
    "Supporting markers: $($supportNeedles -join ', ')",
    "This scan is read-only. It does not execute firmware binaries or access a live device node.",
    "A generic JNI_OnLoad or RegisterNatives marker is supporting evidence only, not proof of the CD3313 bridge."
) | Out-File -LiteralPath $summaryPath -Encoding utf8

$zipPath = "$OutputDir.zip"
Compress-Archive -Path (Join-Path $OutputDir "*") -DestinationPath $zipPath -Force
Write-Host "Candidate archive: $zipPath"
