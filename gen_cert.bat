@echo off
setlocal

:: Generate a self-signed wildcard certificate for *.localhost.com (10 years)
:: Output: data\ssl\server.crt, data\ssl\server.key

set "OPENSSL=%ProgramFiles%\Git\usr\bin\openssl.exe"
set "DOMAIN=localhost.com"
set "DAYS=3650"
set "OUT_DIR=%~dp0data\ssl"
set "CNF=%OUT_DIR%\openssl.cnf"

if not exist "%OPENSSL%" (
    echo [ERROR] openssl not found: %OPENSSL%
    exit /b 1
)

if exist "%OUT_DIR%\server.key" (
    echo [ERROR] %OUT_DIR%\server.key already exists, remove it first to regenerate.
    exit /b 1
)

if not exist "%OUT_DIR%" mkdir "%OUT_DIR%"

:: Subject and SAN go through a config file: Git's MSYS openssl would mangle
:: "/CN=..." as a POSIX path and may glob "*" when passed on the command line.
(
    echo [req]
    echo prompt = no
    echo distinguished_name = dn
    echo x509_extensions = v3
    echo [dn]
    echo CN = *.%DOMAIN%
    echo [v3]
    echo basicConstraints = critical, CA:FALSE
    echo keyUsage = critical, digitalSignature, keyEncipherment
    echo extendedKeyUsage = serverAuth
    echo subjectAltName = DNS:*.%DOMAIN%, DNS:%DOMAIN%
) > "%CNF%"

"%OPENSSL%" req -x509 -newkey rsa:2048 -nodes -sha256 -days %DAYS% ^
    -config "%CNF%" -keyout "%OUT_DIR%\server.key" -out "%OUT_DIR%\server.crt"
set "RC=%errorlevel%"
del "%CNF%"
if %RC% neq 0 (
    echo [ERROR] Certificate generation failed.
    exit /b 1
)

echo.
"%OPENSSL%" x509 -in "%OUT_DIR%\server.crt" -noout -subject -enddate -ext subjectAltName

echo.
echo Done. Add to data\config.json:
echo   "protocol": "https",
echo   "ssl_cert": "data/ssl/server.crt",
echo   "ssl_key": "data/ssl/server.key"

endlocal
