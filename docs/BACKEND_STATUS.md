# Backend Status

The backend now exposes API routes for every app page and is wired to MySQL Workbench through `DATABASE_URL`.

Verified locally:

- `npm run build`
- `npm run smoke:api`

The smoke test starts the API on a temporary localhost port, checks the web entry page and moved image asset, calls every route below, creates temporary mutation records, then removes those temporary rows.

- `GET /api/health`
- `POST /api/auth/login`
- `POST /api/auth/signup`
- `GET /api/staff-accounts?creatorRole=admin`
- `POST /api/staff-accounts`
- `GET /api/summary`
- `GET /api/dashboard`
- `GET /api/process`
- `POST /api/process/actions`
- `GET /api/iot`
- `GET /api/contributions`
- `GET /api/rewards`
- `POST /api/rewards/redeem`
- `GET /api/reports`
- `GET /api/history`
- `GET /api/settings`
- `PUT /api/settings`
- `GET /api/qr-scans`
- `POST /api/qr-scans`
- `POST /api/access-requests`

Backend structure:

- `backend/server.js`: entry point
- `backend/src/http/apiRouter.js`: API routes
- `backend/src/db/mysqlStore.js`: MySQL-backed data store
- `backend/src/db/fallbackStore.js`: demo fallback data
- `backend/src/data/pageData.js`: page data contracts and fallback payloads
- `scripts/smoke-api.js`: route-level backend smoke test
- `database/mysql/mysql_schema.sql`: MySQL Workbench schema and seed data

Important:

- Re-run/import `database/mysql/mysql_schema.sql` in MySQL Workbench to create the new backend tables.
- Routes return fallback demo payloads if a newer table is missing, so the API can still respond during setup.
- Web login supports Admin, Barangay Staff, and User roles from one login screen.
- Admin web users can create Barangay Staff accounts through `POST /api/staff-accounts`.
- Android currently uses the backend for user login and signup, and stays contributor-focused for the mobile/QR flow.
