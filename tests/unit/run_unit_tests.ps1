$ErrorActionPreference = "Stop"

$repoRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$binDir = Join-Path $PSScriptRoot "bin"
New-Item -ItemType Directory -Force -Path $binDir | Out-Null

$includeDirs = @(
    (Join-Path $PSScriptRoot "stubs")
)

$includeDirs += Get-ChildItem `
    (Join-Path $repoRoot "components"), `
    (Join-Path $repoRoot "Core"), `
    (Join-Path $repoRoot "platform"), `
    (Join-Path $repoRoot "config") `
    -Recurse -Filter *.h | ForEach-Object { $_.DirectoryName } | Sort-Object -Unique

$includeArgs = $includeDirs | ForEach-Object { "-I$_" }
$commonArgs = @("-std=gnu11", "-Wall", "-Wextra", "-Werror") + $includeArgs

$tests = @(
    @{
        Name = "test_ecum"
        Sources = @(
            (Join-Path $PSScriptRoot "test_ecum.c"),
            (Join-Path $repoRoot "components\core_base\ecum\ecum.c")
        )
    },
    @{
        Name = "test_rtc_manager"
        Sources = @(
            (Join-Path $PSScriptRoot "test_rtc_manager.c"),
            (Join-Path $repoRoot "components\core_base\rtc_manager\rtc_manager.c")
        )
    },
    @{
        Name = "test_hal_pwr"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_pwr.c"),
            (Join-Path $repoRoot "components\hal\pwr\hal_pwr.c")
        )
    },
    @{
        Name = "test_hal_rtc"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_rtc.c"),
            (Join-Path $repoRoot "components\hal\rtc\hal_rtc.c")
        )
    },
    @{
        Name = "test_hal_adc"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_adc.c"),
            (Join-Path $repoRoot "components\hal\adc\hal_adc.c")
        )
    },
    @{
        Name = "test_hal_i2c"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_i2c.c"),
            (Join-Path $repoRoot "components\hal\i2c\hal_i2c.c")
        )
    },
    @{
        Name = "test_hal_spi"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_spi.c"),
            (Join-Path $repoRoot "components\hal\spi\hal_spi.c")
        )
    },
    @{
        Name = "test_hal_uart"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_uart.c"),
            (Join-Path $repoRoot "components\hal\uart\hal_uart.c")
        )
    },
    @{
        Name = "test_hal_gpio"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_gpio.c"),
            (Join-Path $repoRoot "components\hal\gpio\hal_gpio.c")
        )
    },
    @{
        Name = "test_hal_wdg"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_wdg.c"),
            (Join-Path $repoRoot "components\hal\wdg\hal_wdg.c")
        )
    },
    @{
        Name = "test_hal_flash"
        Sources = @(
            (Join-Path $PSScriptRoot "test_hal_flash.c"),
            (Join-Path $repoRoot "components\hal\flash\hal_flash.c")
        )
    }
)

foreach ($test in $tests) {
    $outFile = Join-Path $binDir ($test.Name + ".exe")
    & gcc @commonArgs $test.Sources "-o" $outFile
    if ($LASTEXITCODE -ne 0) { throw "compile failed: $($test.Name)" }
    & $outFile
    if ($LASTEXITCODE -ne 0) { throw "test failed: $($test.Name)" }
}

Write-Host "All unit tests passed."
