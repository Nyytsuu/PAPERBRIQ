$code = (Read-Host 'Enter the six-digit machine key from the phone').Trim()
if ($code -cnotmatch '^[0-9]{6}$') {
    throw 'Enter exactly six digits, including any leading zero.'
}

$secret = Read-Host 'Enter the Render CONTROL_API_KEY (hidden)' -AsSecureString
try {
    $key = [System.Net.NetworkCredential]::new('', $secret).Password.Trim()
    $null = Invoke-RestMethod -Method Post `
        -Uri 'https://paperbriq.onrender.com/api/auth/machine-key/approve' `
        -ContentType 'application/json' `
        -Headers @{ 'X-Control-Key' = $key } `
        -Body (@{ code = $code } | ConvertTo-Json -Compress) `
        -MaximumRedirection 0 -TimeoutSec 60 -ErrorAction Stop
    Write-Output 'Approved. On the phone, paste the six-digit key and tap Approve phone.'
} catch {
    throw 'Approval failed. Check that Render is updated, the administrator key is correct, and the six-digit request is still active.'
} finally {
    Remove-Variable key -ErrorAction SilentlyContinue
    $secret.Dispose()
}
