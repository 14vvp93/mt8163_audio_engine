# Extracted HEQ APK

The original 161 MiB APK is stored as three GitHub-safe chunks. Reassemble from the repository root with:

    cat reference-materials/extracted/heq/HEQ.apk.part* > HEQ.apk

Verify the resulting file against the SHA-256 in HEQ_APK_MANIFEST.json. The original split HEQ ZIP volumes remain unchanged in reference-materials/heq/.
