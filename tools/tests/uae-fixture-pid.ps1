(Get-CimInstance Win32_Process -Filter "Name='winuae.exe'" | Where-Object { $_.CommandLine -like '*ue1-rc17-hang*' } | Select-Object -First 1).ProcessId
