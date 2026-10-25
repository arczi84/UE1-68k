Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" | Where-Object { $_.CommandLine -like '*ue1-rc17-hang*' } | ForEach-Object { Stop-Process -Id $_.ProcessId -Force; "killed $($_.ProcessId)" }
