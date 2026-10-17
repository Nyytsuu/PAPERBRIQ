# PaperBriq

PaperBriq includes a React/Vite monitoring and control system plus a native Android app. Authorized Admin and Barangay Staff users can monitor live telemetry and queue Start, Software Stop, and Reset Fault commands. The ESP32 exchanges commands and events with the Node API, while an Arduino Uno controls the physical actuators.

## Clean Project Layout

```text
backend/
  server.js
  src/
    config/
    data/
    db/
    http/
    server/
frontend/
  android/
  web/
    src/
    assets/images/
    dist/
database/
  mysql/
  postgres/
docs/
scripts/
```

Root-level files are kept for project tooling: `package.json`, `.env`, `.env.example`, `docker-compose.yml`, and lock/config files.

## Run the Backend and Built Web System

```powershell
npm install
npm run build
npm start
```

Open http://localhost:4173.

Without a database connection, the API runs in demo-memory mode.

The web system provides live monitoring and authorized machine controls for Admin and Barangay Staff. Contributor/user accounts use the Android app.

For React development with Vite hot reload, run the backend and Vite in two terminals:

```powershell
npm start
```

```powershell
npm run web:dev
```

Then open http://localhost:5173.

## Run in Android Studio

Open the `frontend/android` folder in Android Studio, then sync the project and run it on a Google Pixel 7 emulator.

Before logging in from the emulator, start the backend from this repo:

```powershell
npm start
```

The Android app uses `https://paperbriq.onrender.com` by default, followed by local development fallback addresses. To build against a different backend, override the URL at build time instead of editing Java source:

```powershell
cd frontend/android
$env:PAPERBRIQ_API_BASE_URL="https://your-other-service.onrender.com"
.\gradlew.bat :app:assembleDebug
```

Install `app/build/outputs/apk/debug/app-debug.apk`. The phone does not need to be on the laptop's Wi-Fi when both the phone and ESP32 use the public Render URL.

This is a native Java Android app, not a Capacitor app. Do not use `npx cap run android` unless the project is later converted to Capacitor.

Useful native Android npm helpers:

```powershell
npm run android:bundled:sync
npm run android:devices
npm run android:reverse -- 12889374AD000249
npm run android:assemble
npm run android:run -- 12889374AD000249
```

`android:assemble` and `android:run` require Gradle or a Gradle wrapper. If this terminal cannot find Gradle, open `frontend/android` in Android Studio and run the app there.

For a real USB phone, run `npm run android:reverse -- 12889374AD000249` after `npm start`. This maps the phone's `127.0.0.1:4173` to the backend on your computer. The Android app also falls back to the current Wi-Fi address `192.168.1.3:4173` and emulator address `10.0.2.2:4173`.

The mobile app should not connect directly to the database. It talks to the Node API, and the Node API talks to MySQL.

Android machine alerts are generated while the app is open and polling the backend. Reliable notifications while the app is fully closed require a push service such as Firebase Cloud Messaging, which is not included yet.

Implemented Android pages are tracked in `docs/ANDROID_PAGES_STATUS.md`.

## MySQL Workbench Setup

Use MySQL Workbench to import:

```text
database/mysql/mysql_schema.sql
```

That script creates the `paperbriq` database, tables, view, and demo data.

Your local backend `.env` should use:

```text
DB_CLIENT=mysql
DATABASE_URL=mysql://root:your_mysql_password@127.0.0.1:3306/paperbriq
```

Then start the API:

```powershell
npm start
```

## Checks

Run the backend build/check:

```powershell
npm run build
```

Run the backend API smoke test:

```powershell
npm run smoke:api
```

Backend page endpoints are listed in `docs/BACKEND_STATUS.md`.

## Render Remote-Control Setup

Add these environment variables to the Render web service, then redeploy it:

```text
REMOTE_CONTROL_ENABLED=true
CONTROL_API_KEY=<long random operator key>
IOT_API_KEY=<different long random device key>
DEVICE_COMMAND_TTL_SECONDS=120
DEVICE_ONLINE_SECONDS=60
```

Set `PAPERBRIQ_API_BASE_URL` in the ESP32 `secrets.h` to the same Render HTTPS origin, set `PAPERBRIQ_IOT_API_KEY` to the Render `IOT_API_KEY`, and keep `PAPERBRIQ_DEVICE_ID` as `paperbriq-esp32`. The web and Android control screens ask for `CONTROL_API_KEY`; they do not store it permanently.

The ESP32 must verify Render's HTTPS certificate. Put the correct PEM root CA in `PAPERBRIQ_TLS_ROOT_CA`. `PAPERBRIQ_ALLOW_INSECURE_TLS=1` is for temporary diagnosis only and must not be used for an internet-connected machine controller.

The command path is:

```text
Web/Android -> Render API -> ESP32 -> UART -> Arduino Uno -> actuators
Arduino Uno -> UART events -> ESP32 -> Render API -> Web/Android alerts
```

`Software Stop` only asks the controllers to stop. It is not an emergency stop and cannot remove motor power. Keep a hard-wired, reachable emergency disconnect. Reset only clears/arms the stopped software state and never starts motion; Start is accepted only after Reset has armed the Uno.

## PostgreSQL Setup, Optional

```powershell
docker compose up -d postgres
Copy-Item .env.example .env
npm start
```

The Docker database loads PostgreSQL files from `database/postgres`.

Demo accounts:

| Role | Email | Password |
| --- | --- | --- |
| Admin | admin@paperbriq.local | paperbriq123 |
| Barangay Staff | staff@paperbriq.local | paperbriq123 |
| User | contributor@paperbriq.local | paperbriq123 |

## Included Pieces

- Native Android Studio project in `frontend/android`
- React/Vite web system and image assets in `frontend/web`
- PaperBriq branding throughout the app
- Node API server in `backend`
- MySQL Workbench schema in `database/mysql`
- Optional PostgreSQL schema and seed in `database/postgres`
