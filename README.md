# PaperBriq

PaperBriq includes a read-only React/Vite monitoring system for Admin and Barangay Staff plus a native Android app for the contributor/mobile flow. The monitoring system surfaces live machine telemetry, alerts, production metrics, and performance history. The backend uses a Node API with MySQL Workbench support.

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

The web system is a React/Vite monitoring dashboard for Admin and Barangay Staff. Account management, station controls, rewards, and configuration are not exposed in the web interface. Contributor/user accounts use the Android app.

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

The Android app uses `http://10.0.2.2:4173` by default. That address points from the Android emulator back to your computer. For a real Android phone, change `API_BASE_URL` in `frontend/android/app/src/main/java/com/paperbriq/app/MainActivity.java` to your computer's LAN IP address, for example `http://192.168.1.25:4173`.

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
