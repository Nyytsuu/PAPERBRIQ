# Android Pages Status

Finished as separate Android screen files under `frontend/android/app/src/main/java/com/paperbriq/app`:

- `MainActivity.java`: Android entry point only
- `AppController.java`: navigation, login flow, access request flow
- `data/ApiClient.java`: backend API connection at `http://10.0.2.2:4173`
- `data/AppSession.java`: logged-in user and dashboard summary state
- `ui/LoginScreen.java`: login page with PaperBriq branding
- `ui/AppShell.java`: shared page header and horizontal navigation
- `ui/Ui.java`: shared card, button, chart, list, and typography helpers
- `screens/DashboardScreen.java`
- `screens/ProcessTrackingScreen.java`
- `screens/IotMonitoringScreen.java`
- `screens/ContributionsScreen.java`
- `screens/RewardsScreen.java`
- `screens/ReportsScreen.java`
- `screens/HistoryScreen.java`
- `screens/SettingsScreen.java`
- `screens/ScanQrScreen.java`

Paused at:

- Android Studio/Gradle build verification. The current shell does not expose Android SDK/Gradle, and `gradle assembleDebug` fails because `gradle` is not recognized in this terminal. Final compile/sync must be done in Android Studio.
- Real camera QR scanning. The Scan QR page is implemented as a UI screen with manual submit and scan results mock data; camera scanning needs a QR/camera library or Android CameraX integration.

Verified here:

- `npm run build` passes for the Node backend/modules.
- Backend health check starts in MySQL mode.
- Backend page endpoints are complete and listed in `docs/BACKEND_STATUS.md`.
- Previous reference branding text is not present in the app code.

Database note:

- MySQL Workbench should import `database/mysql/mysql_schema.sql`.
- Start the backend with `npm start` before logging in from the Android emulator.
