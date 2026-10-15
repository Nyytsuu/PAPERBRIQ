const fs = require("fs");
const path = require("path");
const { spawnSync } = require("child_process");

const rootDir = path.join(__dirname, "..");
const androidDir = path.join(rootDir, "frontend", "android");
const packageName = "com.paperbriq.app";
const mainActivity = `${packageName}/.MainActivity`;

const command = process.argv[2] || "help";
const target = process.argv[3];

function main() {
  if (!fs.existsSync(androidDir)) {
    fail(`Android project not found at ${androidDir}`);
  }

  if (command === "sync") {
    console.log("PaperBriq native Android project is ready at frontend/android.");
    console.log("No Capacitor sync is needed for this native Java Android app.");
    console.log("Open frontend/android in Android Studio, or use npm run android:assemble when Gradle is on PATH.");
    return;
  }

  if (command === "devices") {
    runAdb(["devices"]);
    return;
  }

  if (command === "reverse") {
    runAdb(adbArgs(target, ["reverse", "tcp:4173", "tcp:4173"]));
    console.log("ADB reverse is active: device localhost:4173 -> computer localhost:4173");
    return;
  }

  if (command === "assemble") {
    assemble();
    return;
  }

  if (command === "install") {
    install(target);
    return;
  }

  if (command === "run") {
    install(target);
    runAdb(adbArgs(target, ["shell", "am", "start", "-n", mainActivity]));
    return;
  }

  console.log("Usage:");
  console.log("  npm run android:bundled:sync");
  console.log("  npm run android:devices");
  console.log("  npm run android:reverse -- <device-id>");
  console.log("  npm run android:assemble");
  console.log("  npm run android:install -- <device-id>");
  console.log("  npm run android:run -- <device-id>");
}

function assemble() {
  const gradle = findGradle();
  if (!gradle) {
    fail(
      "Gradle was not found on PATH and no Gradle wrapper exists.\n" +
      "Open frontend/android in Android Studio and run/sync there, or add Gradle/gradlew to the project."
    );
  }

  run(gradle.command, [...gradle.prefixArgs, "assembleDebug"], androidDir);
}

function install(deviceId) {
  assemble();

  const apkPath = path.join(androidDir, "app", "build", "outputs", "apk", "debug", "app-debug.apk");
  if (!fs.existsSync(apkPath)) {
    fail(`Debug APK was not created at ${apkPath}`);
  }

  runAdb(adbArgs(deviceId, ["install", "-r", apkPath]));
}

function runAdb(args) {
  const adb = findAdb();
  if (!adb) {
    fail(
      "adb was not found.\n" +
      "Install Android SDK Platform Tools or set ANDROID_HOME/ANDROID_SDK_ROOT to your Android SDK folder."
    );
  }

  run(adb, args, rootDir);
}

function adbArgs(deviceId, args) {
  return deviceId ? ["-s", deviceId, ...args] : args;
}

function findGradle() {
  const wrapper = process.platform === "win32"
    ? path.join(androidDir, "gradlew.bat")
    : path.join(androidDir, "gradlew");

  if (fs.existsSync(wrapper)) {
    return {
      command: process.platform === "win32" ? "cmd" : wrapper,
      prefixArgs: process.platform === "win32" ? ["/c", wrapper] : []
    };
  }

  const gradle = findOnPath("gradle");
  return gradle ? { command: gradle, prefixArgs: [] } : null;
}

function findAdb() {
  const candidates = [
    findOnPath("adb"),
    process.env.ANDROID_HOME && path.join(process.env.ANDROID_HOME, "platform-tools", executable("adb")),
    process.env.ANDROID_SDK_ROOT && path.join(process.env.ANDROID_SDK_ROOT, "platform-tools", executable("adb")),
    process.env.LOCALAPPDATA && path.join(process.env.LOCALAPPDATA, "Android", "Sdk", "platform-tools", executable("adb"))
  ].filter(Boolean);

  return candidates.find((candidate) => fs.existsSync(candidate)) || null;
}

function findOnPath(name) {
  const finder = process.platform === "win32" ? "where.exe" : "which";
  const result = spawnSync(finder, [name], { encoding: "utf8" });
  if (result.status !== 0) return null;

  return result.stdout
    .split(/\r?\n/)
    .map((line) => line.trim())
    .find(Boolean) || null;
}

function executable(name) {
  return process.platform === "win32" ? `${name}.exe` : name;
}

function run(commandPath, args, cwd) {
  const result = spawnSync(commandPath, args, {
    cwd,
    stdio: "inherit",
    shell: false
  });

  if (result.error) {
    fail(result.error.message);
  }

  if (result.status !== 0) {
    process.exit(result.status || 1);
  }
}

function fail(message) {
  console.error(message);
  process.exit(1);
}

main();
