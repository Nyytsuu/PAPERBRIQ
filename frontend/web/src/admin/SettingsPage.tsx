import React, { useState } from "react";
import { useOutletContext } from "react-router-dom";
import { ConfirmModal, Panel } from "../components/ui";

const adminSettingsGroups = [
  {
    key: "profile",
    title: "Profile Settings",
    subtitle: "Personal CENRO or main admin account details.",
    scope: "Admin account",
    fields: ["Admin name", "Email address", "Contact number", "Username", "Password change", "Profile picture"],
    actions: ["Update personal account details", "Change login credentials", "Review profile information"]
  },
  {
    key: "accounts",
    title: "Account and Role Management",
    subtitle: "Create, edit, disable, and assign system users.",
    scope: "System-wide",
    fields: ["Add barangay staff account", "Edit staff account", "Disable staff account", "Assign staff to barangay", "Set role: Admin, Barangay Staff, Contributor", "Reset staff password", "View account status"],
    actions: ["Create barangay staff accounts", "Assign access levels", "Reset staff passwords"]
  },
  {
    key: "barangays",
    title: "Barangay Management",
    subtitle: "Control barangay records, staff assignments, and machines.",
    scope: "All barangays",
    fields: ["Barangay name", "Barangay address", "Contact person", "Contact number", "Assigned staff", "Machine ID"],
    actions: ["Add or edit barangays", "Assign machines", "Disable inactive barangays", "View barangay performance"]
  },
  {
    key: "machine",
    title: "Machine Settings",
    subtitle: "Configure EcoBlock machine rules and maintenance timing.",
    scope: "Machine control",
    fields: ["Machine name / Machine ID", "Assigned barangay", "Machine status", "Shredding time: 5 minutes", "Mixing time: 3 minutes", "Pressing time: 2 minutes", "Drying temperature: 45 C to 55 C", "Drying time: 20 to 40 minutes", "Safety interlock: Enabled"],
    actions: ["Update machine limits", "Set maintenance schedule", "Toggle safety mode"]
  },
  {
    key: "rewards",
    title: "Reward and Points Settings",
    subtitle: "Manage reward rules that affect all contributors.",
    scope: "System-wide rewards",
    fields: ["1 kg paper waste = 10 points", "100 points = P100 voucher / eco item", "300 points = Eco tote bag", "500 points = Tree planting certificate", "Redemption requirements", "Reward claim approval"],
    actions: ["Add reward item", "Edit or disable reward item", "Approve reward claims", "View reward history"]
  },
  {
    key: "qr",
    title: "QR Code Settings",
    subtitle: "Control QR generation, scanning, and validation rules.",
    scope: "System QR rules",
    fields: ["Generate user QR code", "Set QR expiration", "Enable or disable QR scanning", "Prevent duplicate scans", "One QR scan per transaction", "Staff enters paper weight after scanning", "Points added after staff confirmation"],
    actions: ["Generate QR codes", "View QR scan logs", "Set validation rules"]
  },
  {
    key: "reports",
    title: "Report Settings",
    subtitle: "Manage reports across all barangays.",
    scope: "All reports",
    fields: ["Weekly report", "Monthly report", "Barangay performance report", "Paper collected report", "Briquettes produced report", "Rewards redeemed report"],
    actions: ["Export as PDF / Excel", "Print report", "Archive reports"]
  },
  {
    key: "notifications",
    title: "Notification Settings",
    subtitle: "Choose which system alerts should notify admins.",
    scope: "Admin alerts",
    fields: ["Low rainwater alert", "Machine error alert", "Maintenance reminder", "Low battery alert", "Reward redemption alert", "Staff activity alert"],
    actions: ["Enable alert types", "Review staff activity alerts"]
  },
  {
    key: "security",
    title: "Security Settings",
    subtitle: "Protect admin access and review account activity.",
    scope: "Admin security",
    fields: ["Change password", "Two-factor authentication", "Login history", "Account lockout after failed attempts", "Session timeout", "Activity logs"],
    actions: ["Update security rules", "Review login history", "Set lockout policy"]
  },
  {
    key: "backup",
    title: "System Backup Settings",
    subtitle: "Backup, restore, export, and archive system data.",
    scope: "Database safety",
    fields: ["Backup database", "Restore database", "Export records", "Clear old logs", "Archive reports"],
    actions: ["Run database backup", "Restore saved data", "Export long-term records"]
  }
];

const staffSettingsGroups = [
  {
    key: "profile",
    title: "Profile Settings",
    subtitle: "Personal barangay staff account information.",
    scope: "Staff account",
    fields: ["Staff name", "Email / username", "Contact number", "Assigned barangay", "Change password", "Profile picture"],
    actions: ["Update personal account", "Change password", "Review assigned barangay"]
  },
  {
    key: "barangay",
    title: "Barangay Information",
    subtitle: "View local barangay details without changing system records.",
    scope: "View only",
    fields: ["Barangay name", "Barangay address", "Assigned machine", "Contact person", "Staff members"],
    actions: ["View barangay info", "Request detail changes", "Check assigned machine"]
  },
  {
    key: "machine",
    title: "Machine Operation Settings",
    subtitle: "Operate approved machine controls beside the station.",
    scope: "Local machine",
    fields: ["Start process", "Stop process", "Pause process", "Input paper weight", "View current process", "View machine status", "Emergency stop status", "Drying temperature limit: View only", "Safety settings: Locked"],
    actions: ["Start or stop process", "Select briquette size", "Input paper weight"]
  },
  {
    key: "qr",
    title: "QR Scan Settings",
    subtitle: "Record contributor donations and confirm earned points.",
    scope: "Barangay QR flow",
    fields: ["Scan contributor QR code", "Enter paper weight donated", "Confirm contribution", "View recent scans", "Cancel wrong scan", "Mark transaction as verified"],
    actions: ["Scan QR", "Enter weight", "Confirm points"]
  },
  {
    key: "rewards",
    title: "Reward Claim Settings",
    subtitle: "Assist contributors with reward claiming without editing reward rules.",
    scope: "Claim support",
    fields: ["View user points", "Check available rewards", "Add reward item", "Confirm reward claim", "Mark reward as released", "View redemption history"],
    actions: ["Add reward item", "Confirm reward claim", "Mark released rewards", "View redemption history"]
  },
  {
    key: "reports",
    title: "Local Report Settings",
    subtitle: "Generate reports only for the assigned barangay.",
    scope: "Assigned barangay only",
    fields: ["Daily paper collection report", "Weekly briquette production report", "Contributor list", "Reward claim report", "Export barangay report"],
    actions: ["Generate local report", "Export barangay report", "Review contributor list"]
  },
  {
    key: "notifications",
    title: "Notification Settings",
    subtitle: "Receive operational alerts for the local station.",
    scope: "Staff alerts",
    fields: ["Process complete alert", "Machine error alert", "Low water alert", "Maintenance reminder", "Reward claim alert"],
    actions: ["Enable process alerts", "Review maintenance reminders"]
  },
  {
    key: "maintenance",
    title: "Maintenance Settings",
    subtitle: "Log basic cleaning and machine issues.",
    scope: "Local maintenance",
    fields: ["Mark shredder cleaned", "Mark drying tray cleaned", "Report machine issue", "Upload maintenance photo", "View maintenance schedule"],
    actions: ["Log cleaning", "Report machine issue", "View maintenance schedule"]
  }
];

const webSettingsDefaults = {
  admin: {
    profile: {
      adminName: "PaperBriq Admin",
      email: "admin@paperbriq.local",
      contactNumber: "+63 912 345 6789",
      username: "paperbriq.admin",
      passwordPolicy: "Require 8 characters",
      profilePicture: "Use PaperBriq logo"
    },
    accounts: {
      staffCreation: true,
      defaultStaffRole: "Barangay Staff",
      contributorAccess: "Mobile app only",
      passwordReset: true,
      disabledAccountReview: true
    },
    barangays: {
      barangayName: "San Isidro",
      barangayAddress: "Municipal collection station",
      contactPerson: "Barangay Coordinator",
      contactNumber: "+63 900 111 2222",
      assignedStaff: "Assigned barangay staff",
      machineId: "PBQ-MCH-001",
      inactiveBarangays: false
    },
    machine: {
      machineName: "PaperBriq Station 01",
      assignedBarangay: "San Isidro",
      machineStatus: "Running",
      shredTime: 5,
      mixTime: 3,
      pressTime: 2,
      dryingTemperature: 55,
      dryingTime: 30,
      safetyInterlock: true
    },
    rewards: {
      pointsPerKg: 10,
      voucher100: true,
      toteBag: true,
      treeCertificate: true,
      approvalRequired: true,
      rewardHistory: true
    },
    qr: {
      qrExpiration: "24 Hours",
      qrScanning: true,
      duplicateScanPrevention: true,
      requirePaperWeight: true,
      requireStaffConfirmation: true,
      scanLogs: true
    },
    reports: {
      weeklyReport: true,
      monthlyReport: true,
      barangayPerformance: true,
      exportFormat: "PDF and Excel",
      printReports: true,
      archiveReports: true
    },
    notifications: {
      lowRainwater: true,
      machineError: true,
      maintenanceReminder: true,
      lowBattery: true,
      rewardRedemption: true,
      staffActivity: true
    },
    security: {
      twoFactorAuth: false,
      failedAttemptLimit: 5,
      sessionTimeout: "30 Minutes",
      loginHistory: true,
      backupRestoreAccess: true,
      activityLogs: true
    },
    backup: {
      backupSchedule: "Weekly",
      exportRecords: true,
      clearOldLogsAfter: "90 Days",
      archiveReports: true,
      restoreApproval: "Admin only"
    }
  },
  barangay_staff: {
    profile: {
      staffName: "Barangay Staff",
      email: "staff@paperbriq.local",
      contactNumber: "+63 900 111 2222",
      assignedBarangay: "San Isidro",
      passwordPolicy: "Can change own password",
      profilePicture: "Use staff avatar"
    },
    barangay: {
      barangayName: "San Isidro",
      barangayAddress: "Municipal collection station",
      assignedMachine: "PBQ-MCH-001",
      contactPerson: "Barangay Coordinator",
      staffMembers: "3 active staff",
      editRequest: true
    },
    machine: {
      briquetteSize: "Standard (42x22 cm)",
      paperWeightInput: true,
      processControl: true,
      machineStatus: "View only",
      dryingTemperatureLimit: "View only",
      safetySettings: "Locked"
    },
    qr: {
      scanQr: true,
      enterPaperWeight: true,
      confirmContribution: true,
      viewRecentScans: true,
      cancelWrongScan: true,
      verifyTransaction: true
    },
    rewards: {
      viewUserPoints: true,
      checkAvailableRewards: true,
      confirmRewardClaim: true,
      markRewardReleased: true,
      viewRedemptionHistory: true,
      editRewardItems: true
    },
    reports: {
      dailyCollectionReport: true,
      weeklyProductionReport: true,
      contributorList: true,
      rewardClaimReport: true,
      exportBarangayReport: true,
      reportScope: "Assigned barangay only"
    },
    notifications: {
      processComplete: true,
      machineError: true,
      lowWater: true,
      maintenanceReminder: true,
      rewardClaim: true
    },
    maintenance: {
      shredderCleaned: false,
      dryingTrayCleaned: false,
      reportMachineIssue: true,
      uploadMaintenancePhoto: true,
      maintenanceSchedule: "Every Friday"
    }
  }
};

const settingsControlMap = {
  admin: {
    profile: [
      { path: "profile.adminName", label: "Admin name", type: "text" },
      { path: "profile.email", label: "Email address", type: "email" },
      { path: "profile.contactNumber", label: "Contact number", type: "text" },
      { path: "profile.username", label: "Username", type: "text" },
      { path: "profile.passwordPolicy", label: "Password rule", type: "select", options: ["Require 8 characters", "Require 12 characters", "Require numbers and symbols"] },
      { path: "profile.profilePicture", label: "Profile picture", type: "select", options: ["Use PaperBriq logo", "Use uploaded profile", "Use initials"] }
    ],
    accounts: [
      { path: "accounts.staffCreation", label: "Allow staff account creation", type: "toggle" },
      { path: "accounts.defaultStaffRole", label: "Default staff role", type: "select", options: ["Barangay Staff", "Admin", "Contributor"] },
      { path: "accounts.contributorAccess", label: "Contributor access", type: "select", options: ["Mobile app only", "Mobile and web request", "Disabled"] },
      { path: "accounts.passwordReset", label: "Allow staff password reset", type: "toggle" },
      { path: "accounts.disabledAccountReview", label: "Review disabled accounts", type: "toggle" }
    ],
    barangays: [
      { path: "barangays.barangayName", label: "Barangay name", type: "text" },
      { path: "barangays.barangayAddress", label: "Barangay address", type: "text" },
      { path: "barangays.contactPerson", label: "Contact person", type: "text" },
      { path: "barangays.contactNumber", label: "Contact number", type: "text" },
      { path: "barangays.assignedStaff", label: "Assigned staff", type: "text" },
      { path: "barangays.machineId", label: "Machine ID", type: "text" },
      { path: "barangays.inactiveBarangays", label: "Allow inactive barangays", type: "toggle" }
    ],
    machine: [
      { path: "machine.machineName", label: "Machine name", type: "text" },
      { path: "machine.assignedBarangay", label: "Assigned barangay", type: "text" },
      { path: "machine.machineStatus", label: "Machine status", type: "select", options: ["Running", "Idle", "Maintenance", "Locked"] },
      { path: "machine.shredTime", label: "Shredding time (minutes)", type: "number", min: 1, max: 60 },
      { path: "machine.mixTime", label: "Mixing time (minutes)", type: "number", min: 1, max: 60 },
      { path: "machine.pressTime", label: "Pressing time (minutes)", type: "number", min: 1, max: 60 },
      { path: "machine.dryingTemperature", label: "Drying temperature (C)", type: "number", min: 40, max: 80 },
      { path: "machine.dryingTime", label: "Drying time (minutes)", type: "number", min: 10, max: 120 },
      { path: "machine.safetyInterlock", label: "Safety interlock", type: "toggle" }
    ],
    rewards: [
      { path: "rewards.pointsPerKg", label: "Points per kilo", type: "number", min: 1, max: 100 },
      { path: "rewards.voucher100", label: "P100 voucher enabled", type: "toggle" },
      { path: "rewards.toteBag", label: "Eco tote bag enabled", type: "toggle" },
      { path: "rewards.treeCertificate", label: "Tree certificate enabled", type: "toggle" },
      { path: "rewards.approvalRequired", label: "Require admin approval", type: "toggle" },
      { path: "rewards.rewardHistory", label: "Keep reward history", type: "toggle" }
    ],
    qr: [
      { path: "qr.qrExpiration", label: "QR expiration", type: "select", options: ["12 Hours", "24 Hours", "48 Hours", "7 Days"] },
      { path: "qr.qrScanning", label: "Enable QR scanning", type: "toggle" },
      { path: "qr.duplicateScanPrevention", label: "Prevent duplicate scans", type: "toggle" },
      { path: "qr.requirePaperWeight", label: "Require paper weight", type: "toggle" },
      { path: "qr.requireStaffConfirmation", label: "Require staff confirmation", type: "toggle" },
      { path: "qr.scanLogs", label: "Store scan logs", type: "toggle" }
    ],
    reports: [
      { path: "reports.weeklyReport", label: "Weekly report", type: "toggle" },
      { path: "reports.monthlyReport", label: "Monthly report", type: "toggle" },
      { path: "reports.barangayPerformance", label: "Barangay performance report", type: "toggle" },
      { path: "reports.exportFormat", label: "Export format", type: "select", options: ["PDF", "Excel", "PDF and Excel"] },
      { path: "reports.printReports", label: "Print reports", type: "toggle" },
      { path: "reports.archiveReports", label: "Archive reports", type: "toggle" }
    ],
    notifications: [
      { path: "notifications.lowRainwater", label: "Low rainwater alert", type: "toggle" },
      { path: "notifications.machineError", label: "Machine error alert", type: "toggle" },
      { path: "notifications.maintenanceReminder", label: "Maintenance reminder", type: "toggle" },
      { path: "notifications.lowBattery", label: "Low battery alert", type: "toggle" },
      { path: "notifications.rewardRedemption", label: "Reward redemption alert", type: "toggle" },
      { path: "notifications.staffActivity", label: "Staff activity alert", type: "toggle" }
    ],
    security: [
      { path: "security.twoFactorAuth", label: "Two-factor authentication", type: "toggle" },
      { path: "security.failedAttemptLimit", label: "Failed attempt limit", type: "number", min: 3, max: 10 },
      { path: "security.sessionTimeout", label: "Session timeout", type: "select", options: ["15 Minutes", "30 Minutes", "1 Hour", "4 Hours"] },
      { path: "security.loginHistory", label: "Track login history", type: "toggle" },
      { path: "security.backupRestoreAccess", label: "Allow backup restore tools", type: "toggle" },
      { path: "security.activityLogs", label: "Activity logs", type: "toggle" }
    ],
    backup: [
      { path: "backup.backupSchedule", label: "Backup schedule", type: "select", options: ["Daily", "Weekly", "Monthly"] },
      { path: "backup.exportRecords", label: "Allow record export", type: "toggle" },
      { path: "backup.clearOldLogsAfter", label: "Clear old logs after", type: "select", options: ["30 Days", "60 Days", "90 Days", "Never"] },
      { path: "backup.archiveReports", label: "Archive reports", type: "toggle" },
      { path: "backup.restoreApproval", label: "Restore approval", type: "select", options: ["Admin only", "Two-admin confirmation", "Disabled"] }
    ]
  },
  barangay_staff: {
    profile: [
      { path: "profile.staffName", label: "Staff name", type: "text" },
      { path: "profile.email", label: "Email / username", type: "email" },
      { path: "profile.contactNumber", label: "Contact number", type: "text" },
      { path: "profile.assignedBarangay", label: "Assigned barangay", type: "text", locked: true },
      { path: "profile.passwordPolicy", label: "Password access", type: "select", options: ["Can change own password", "Request admin reset"], locked: true }
    ],
    barangay: [
      { path: "barangay.barangayName", label: "Barangay name", type: "text", locked: true },
      { path: "barangay.barangayAddress", label: "Barangay address", type: "text", locked: true },
      { path: "barangay.assignedMachine", label: "Assigned machine", type: "text", locked: true },
      { path: "barangay.contactPerson", label: "Contact person", type: "text", locked: true },
      { path: "barangay.staffMembers", label: "Staff members", type: "text", locked: true },
      { path: "barangay.editRequest", label: "Allow edit request", type: "toggle" }
    ],
    machine: [
      { path: "machine.briquetteSize", label: "Briquette size", type: "select", options: ["Small", "Standard (42x22 cm)", "Large"] },
      { path: "machine.paperWeightInput", label: "Paper weight input", type: "toggle" },
      { path: "machine.processControl", label: "Start / stop process", type: "toggle" },
      { path: "machine.machineStatus", label: "Machine status", type: "text", locked: true },
      { path: "machine.dryingTemperatureLimit", label: "Drying temperature limit", type: "text", locked: true },
      { path: "machine.safetySettings", label: "Safety settings", type: "text", locked: true }
    ],
    qr: [
      { path: "qr.scanQr", label: "Scan contributor QR code", type: "toggle" },
      { path: "qr.enterPaperWeight", label: "Enter paper weight", type: "toggle" },
      { path: "qr.confirmContribution", label: "Confirm contribution", type: "toggle" },
      { path: "qr.viewRecentScans", label: "View recent scans", type: "toggle" },
      { path: "qr.cancelWrongScan", label: "Cancel wrong scan", type: "toggle" },
      { path: "qr.verifyTransaction", label: "Mark transaction verified", type: "toggle" }
    ],
    rewards: [
      { path: "rewards.viewUserPoints", label: "View user points", type: "toggle" },
      { path: "rewards.checkAvailableRewards", label: "Check available rewards", type: "toggle" },
      { path: "rewards.confirmRewardClaim", label: "Confirm reward claim", type: "toggle" },
      { path: "rewards.markRewardReleased", label: "Mark reward released", type: "toggle" },
      { path: "rewards.viewRedemptionHistory", label: "View redemption history", type: "toggle" },
      { path: "rewards.editRewardItems", label: "Add reward items", type: "toggle" }
    ],
    reports: [
      { path: "reports.dailyCollectionReport", label: "Daily paper collection report", type: "toggle" },
      { path: "reports.weeklyProductionReport", label: "Weekly briquette production report", type: "toggle" },
      { path: "reports.contributorList", label: "Contributor list", type: "toggle" },
      { path: "reports.rewardClaimReport", label: "Reward claim report", type: "toggle" },
      { path: "reports.exportBarangayReport", label: "Export barangay report", type: "toggle" },
      { path: "reports.reportScope", label: "Report scope", type: "text", locked: true }
    ],
    notifications: [
      { path: "notifications.processComplete", label: "Process complete alert", type: "toggle" },
      { path: "notifications.machineError", label: "Machine error alert", type: "toggle" },
      { path: "notifications.lowWater", label: "Low water alert", type: "toggle" },
      { path: "notifications.maintenanceReminder", label: "Maintenance reminder", type: "toggle" },
      { path: "notifications.rewardClaim", label: "Reward claim alert", type: "toggle" }
    ],
    maintenance: [
      { path: "maintenance.shredderCleaned", label: "Mark shredder cleaned", type: "toggle" },
      { path: "maintenance.dryingTrayCleaned", label: "Mark drying tray cleaned", type: "toggle" },
      { path: "maintenance.reportMachineIssue", label: "Report machine issue", type: "toggle" },
      { path: "maintenance.uploadMaintenancePhoto", label: "Upload maintenance photo", type: "toggle" },
      { path: "maintenance.maintenanceSchedule", label: "Maintenance schedule", type: "select", options: ["Daily", "Every Friday", "Weekly", "Monthly"] }
    ]
  }
};

export default function SettingsPage(props = {}) {
  const outlet = useOutletContext() || {};
  const { user, onSectionChange, onUserUpdate } = { ...outlet, ...props };
  const isAdmin = user.role === "admin";
  const roleKey = isAdmin ? "admin" : "barangay_staff";
  const groups = isAdmin ? adminSettingsGroups : staffSettingsGroups;
  const [activeGroupKey, setActiveGroupKey] = useState(groups[0].key);
  const [settingsValues, setSettingsValues] = useState(() => cloneSettings(webSettingsDefaults));
  const [savedSettingsValues, setSavedSettingsValues] = useState(() => cloneSettings(webSettingsDefaults));
  const [isLoadingSettings, setLoadingSettings] = useState(true);
  const [settingsMessage, setSettingsMessage] = useState("");
  const [settingsMessageType, setSettingsMessageType] = useState("success");
  const [isSavingSettings, setSavingSettings] = useState(false);
  const [pendingConfirmation, setPendingConfirmation] = useState(null);
  const [profileImagePreview, setProfileImagePreview] = useState(user.profileImageUrl || "");
  const [profileImageMessage, setProfileImageMessage] = useState("");
  const [isUploadingProfileImage, setUploadingProfileImage] = useState(false);
  const activeGroup = groups.find((group) => group.key === activeGroupKey) || groups[0];
  const activeControls = settingsControlMap[roleKey][activeGroup.key] || [];
  const canUploadProfileImage = roleKey === "barangay_staff" && activeGroup.key === "profile";
  const userInitial = (user.fullName || user.email || "P").slice(0, 1).toUpperCase();
  const activeGroupValue = getNestedValue(settingsValues[roleKey], activeGroup.key);
  const savedGroupValue = getNestedValue(savedSettingsValues[roleKey], activeGroup.key);
  const isActiveGroupDirty = !sameSettings(activeGroupValue, savedGroupValue);
  const relatedAction = getRelatedSettingsAction(activeGroup.key, isAdmin);

  React.useEffect(() => {
    let isMounted = true;
    setLoadingSettings(true);
    fetch("/api/settings")
      .then((response) => response.json())
      .then((settings) => {
        if (!isMounted) return;
        const mergedSettings = mergeWebSettings(webSettingsDefaults, settings.webSettings || {});
        setSettingsValues(mergedSettings);
        setSavedSettingsValues(cloneSettings(mergedSettings));
      })
      .catch(() => {
        if (!isMounted) return;
        setSettingsMessageType("error");
        setSettingsMessage("Unable to load saved settings. Showing defaults.");
      })
      .finally(() => {
        if (isMounted) setLoadingSettings(false);
      });

    return () => {
      isMounted = false;
    };
  }, []);

  React.useEffect(() => {
    setProfileImagePreview(user.profileImageUrl || "");
  }, [user.profileImageUrl]);

  function updateSetting(path, value) {
    setSettingsValues((current) => ({
      ...current,
      [roleKey]: setNestedValue(current[roleKey], path, value)
    }));
    setSettingsMessage("");
  }

  function handleProfileImageChange(event) {
    const file = event.target.files?.[0];
    event.target.value = "";
    if (!file) return;

    if (!["image/png", "image/jpeg", "image/webp"].includes(file.type)) {
      setProfileImageMessage("Upload a PNG, JPG, or WebP image.");
      return;
    }

    if (file.size > 650 * 1024) {
      setProfileImageMessage("Choose an image under 650 KB.");
      return;
    }

    const reader = new FileReader();
    reader.onload = () => {
      uploadProfileImage(String(reader.result || ""));
    };
    reader.onerror = () => {
      setProfileImageMessage("Unable to read that image. Try another file.");
    };
    reader.readAsDataURL(file);
  }

  async function uploadProfileImage(imageData) {
    setUploadingProfileImage(true);
    setProfileImagePreview(imageData);
    setProfileImageMessage("");

    try {
      const response = await fetch("/api/users/profile-image", {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({
          email: user.email,
          role: user.role,
          imageData
        })
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to upload profile image.");
      onUserUpdate?.(result.user);
      updateSetting("profile.profilePicture", "Use uploaded profile");
      setProfileImageMessage("Profile image uploaded.");
    } catch (error) {
      setProfileImagePreview(user.profileImageUrl || "");
      setProfileImageMessage(error.message);
    } finally {
      setUploadingProfileImage(false);
    }
  }

  async function saveSettings(nextValues = settingsValues) {
    setSavingSettings(true);
    setSettingsMessage("");

    try {
      const response = await fetch("/api/settings", {
        method: "PUT",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ webSettings: nextValues })
      });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error || "Unable to save settings.");
      setSavedSettingsValues(cloneSettings(nextValues));
      setSettingsMessageType("success");
      setSettingsMessage(`${activeGroup.title} saved.`);
    } catch (error) {
      setSettingsMessageType("error");
      setSettingsMessage(error.message);
    } finally {
      setSavingSettings(false);
    }
  }

  function requestSaveSettings() {
    if (!isActiveGroupDirty) {
      setSettingsMessageType("success");
      setSettingsMessage(`${activeGroup.title} already matches the saved settings.`);
      return;
    }

    setPendingConfirmation({
      type: "save",
      title: "Save settings?",
      message: `${activeGroup.title} changes will be saved to the backend and used by this web system.`,
      confirmLabel: "Save changes"
    });
  }

  function requestResetSection() {
    setPendingConfirmation({
      type: "reset",
      title: "Reset this section?",
      message: `${activeGroup.title} will return to the default PaperBriq values. You can review it before saving.`,
      confirmLabel: "Reset section",
      tone: "danger"
    });
  }

  function requestSettingAction(action) {
    setPendingConfirmation({
      type: "action",
      action,
      title: `${action}?`,
      message: getActionMessage(action, isAdmin),
      confirmLabel: getActionConfirmLabel(action)
    });
  }

  async function confirmPendingAction() {
    const pending = pendingConfirmation;
    setPendingConfirmation(null);
    if (!pending) return;

    if (pending.type === "save") {
      await saveSettings();
      return;
    }

    if (pending.type === "reset") {
      resetActiveGroup();
      return;
    }

    await runSettingAction(pending.action);
  }

  function resetActiveGroup() {
    const defaultGroupValue = cloneSettings(getNestedValue(webSettingsDefaults[roleKey], activeGroup.key) || {});
    setSettingsValues((current) => ({
      ...current,
      [roleKey]: {
        ...(current[roleKey] || {}),
        [activeGroup.key]: defaultGroupValue
      }
    }));
    setSettingsMessageType("success");
    setSettingsMessage(`${activeGroup.title} was reset. Save changes to keep it.`);
  }

  async function runSettingAction(action) {
    if (/staff account|role|password/i.test(action) && isAdmin) {
      onSectionChange("staffAccounts");
      return;
    }
    if (/report|export|print|archive/i.test(action)) {
      onSectionChange("reports");
      return;
    }
    if (/reward|claim/i.test(action)) {
      onSectionChange("rewards");
      return;
    }
    if (/machine|maintenance|safety|start|stop|pause|iot/i.test(action)) {
      onSectionChange(isAdmin ? "iot" : "staffTools");
      return;
    }
    if (/scan|qr|weight|points|staff tools/i.test(action) && !isAdmin) {
      onSectionChange("staffTools");
      return;
    }
    if (/backup|restore|clear old logs/i.test(action)) {
      setSettingsMessageType("success");
      setSettingsMessage(`${action} request recorded for admin review.`);
      return;
    }
    setSettingsMessageType("success");
    setSettingsMessage(`${action} is ready. Save changes to apply updated values.`);
  }

  return (
    <section className="settings-workspace">
      <Panel
        title={isAdmin ? "Admin Settings" : "Barangay Staff Settings"}
        subtitle={isAdmin ? "CENRO, main system owner, and super admin controls." : "Limited local settings for barangay operations."}
        action={<span className={`status-pill ${isActiveGroupDirty ? "warning" : ""}`}>{isLoadingSettings ? "Loading" : isActiveGroupDirty ? "Unsaved changes" : "Saved"}</span>}
      >
        <div className="settings-layout">
          <div className="settings-menu" aria-label="Settings categories">
            {groups.map((group) => (
              <button
                key={group.key}
                className={group.key === activeGroup.key ? "active" : ""}
                type="button"
                onClick={() => setActiveGroupKey(group.key)}
              >
                <strong>{group.title}</strong>
                <span>{group.scope}</span>
              </button>
            ))}
          </div>

          <article className="settings-detail">
            <div className="settings-detail-header">
              <div>
                <span>{activeGroup.scope}</span>
                <h3>{activeGroup.title}</h3>
                <p>{activeGroup.subtitle}</p>
              </div>
              <strong className={isActiveGroupDirty ? "settings-state-chip dirty" : "settings-state-chip"}>
                {isActiveGroupDirty ? "Edited" : "Current"}
              </strong>
            </div>

            <div className="settings-control-grid">
              {canUploadProfileImage && (
                <div className="profile-image-card">
                  <span className="profile-image-preview">
                    {profileImagePreview ? <img src={profileImagePreview} alt="" aria-hidden="true" /> : userInitial}
                  </span>
                  <div>
                    <strong>Profile image</strong>
                    <small>Shown in the sidebar and staff account profile.</small>
                    <label className="profile-upload-button">
                      <input
                        type="file"
                        accept="image/png,image/jpeg,image/webp"
                        disabled={isUploadingProfileImage}
                        onChange={handleProfileImageChange}
                      />
                      {isUploadingProfileImage ? "Uploading" : "Choose image"}
                    </label>
                    <p className={`profile-image-message ${profileImageMessage.includes("uploaded") ? "success" : "error"}`}>
                      {profileImageMessage}
                    </p>
                  </div>
                </div>
              )}
              {activeControls.map((control) => (
                <SettingControl
                  key={control.path}
                  control={control}
                  value={getNestedValue(settingsValues[roleKey], control.path)}
                  onChange={(value) => updateSetting(control.path, value)}
                />
              ))}
            </div>

            <div className="settings-actions">
              <div>
                <h4>Section controls</h4>
                <p>Save only the values in this settings section, or reset them back to the default PaperBriq setup.</p>
              </div>
              <div className="settings-action-buttons">
                <button className="settings-save-button" type="button" onClick={requestSaveSettings} disabled={isSavingSettings}>
                  {isSavingSettings ? "Saving" : isActiveGroupDirty ? "Save changes" : "Saved"}
                </button>
                <button className="settings-reset-button" type="button" onClick={requestResetSection}>
                  Reset section
                </button>
                {relatedAction && (
                  <button className="settings-route-button" type="button" onClick={() => requestSettingAction(relatedAction.label)}>
                    {relatedAction.label}
                  </button>
                )}
              </div>
            </div>
            <p className={`form-message left ${settingsMessageType}`}>{settingsMessage}</p>
          </article>
        </div>
      </Panel>

      {pendingConfirmation && (
        <ConfirmModal
          title={pendingConfirmation.title}
          message={pendingConfirmation.message}
          confirmLabel={pendingConfirmation.confirmLabel}
          tone={pendingConfirmation.tone || "default"}
          onCancel={() => setPendingConfirmation(null)}
          onConfirm={confirmPendingAction}
        />
      )}
    </section>
  );
}

function getRelatedSettingsAction(groupKey, isAdmin) {
  const adminActions = {
    accounts: "Open staff accounts",
    barangays: "Open staff accounts",
    machine: "Open IoT monitoring",
    rewards: "Open rewards",
    qr: "Open IoT monitoring",
    reports: "Open reports",
    security: "Review login history",
    backup: "Open reports"
  };
  const staffActions = {
    machine: "Open staff tools",
    qr: "Open staff tools",
    rewards: "Open rewards",
    reports: "Open reports",
    maintenance: "Open staff tools"
  };

  const label = (isAdmin ? adminActions : staffActions)[groupKey];
  return label ? { label } : null;
}

function getActionMessage(action, isAdmin) {
  if (/staff account|role|password/i.test(action) && isAdmin) {
    return "This will open Staff Accounts so the admin can continue managing account access.";
  }
  if (/report|export|print|archive/i.test(action)) {
    return "This will open Reports where the live report data, exports, and history can be reviewed.";
  }
  if (/reward|claim/i.test(action)) {
    return "This will open Rewards so reward items, points, and claims can be managed.";
  }
  if (/machine|maintenance|safety|start|stop|pause/i.test(action)) {
    return isAdmin
      ? "This will open IoT Monitoring so the machine status and safety data can be checked."
      : "This will open Staff Tools so the local machine workflow can be handled.";
  }
  if (/scan|qr|weight|points/i.test(action) && !isAdmin) {
    return "This will open Staff Tools so QR scanning, weights, and points can be recorded.";
  }
  if (/backup|restore|clear old logs/i.test(action)) {
    return "This action will be recorded for admin review before any database maintenance is performed.";
  }
  if (/staff tools/i.test(action)) {
    return "This will open Staff Tools for QR scanning, process actions, and local machine work.";
  }

  return "This action will be prepared for the selected settings category. Save changes afterward if you edited any values.";
}

function getActionConfirmLabel(action) {
  if (/report|export|print|archive/i.test(action)) return "Open reports";
  if (/reward|claim/i.test(action)) return "Open rewards";
  if (/staff account|role|password/i.test(action)) return "Open accounts";
  if (/iot/i.test(action)) return "Open IoT";
  if (/staff tools/i.test(action)) return "Open tools";
  if (/machine|maintenance|safety|start|stop|pause|scan|qr|weight|points/i.test(action)) return "Continue";
  return "Confirm";
}

function SettingControl({ control, value, onChange }) {
  const id = `setting-${control.path.replace(/\./g, "-")}`;
  const disabled = Boolean(control.locked);

  if (control.type === "toggle") {
    return (
      <label className={`setting-control toggle-control ${disabled ? "locked" : ""}`} htmlFor={id}>
        <span>
          <strong>{control.label}</strong>
          <small>{disabled ? "Locked by admin" : value ? "Enabled" : "Disabled"}</small>
        </span>
        <input
          id={id}
          type="checkbox"
          checked={Boolean(value)}
          disabled={disabled}
          onChange={(event) => onChange(event.target.checked)}
        />
        <i aria-hidden="true" />
      </label>
    );
  }

  if (control.type === "select") {
    return (
      <label className={`setting-control ${disabled ? "locked" : ""}`} htmlFor={id}>
        <span>{control.label}</span>
        <select
          id={id}
          value={value ?? ""}
          disabled={disabled}
          onChange={(event) => onChange(event.target.value)}
        >
          {(control.options || []).map((option) => (
            <option key={option} value={option}>{option}</option>
          ))}
        </select>
      </label>
    );
  }

  return (
    <label className={`setting-control ${disabled ? "locked" : ""}`} htmlFor={id}>
      <span>{control.label}</span>
      <input
        id={id}
        type={control.type || "text"}
        value={value ?? ""}
        min={control.min}
        max={control.max}
        disabled={disabled}
        onChange={(event) => onChange(control.type === "number" ? Number(event.target.value) : event.target.value)}
      />
    </label>
  );
}

function mergeWebSettings(defaults, saved) {
  return {
    admin: mergeNested(defaults.admin, saved.admin || {}),
    barangay_staff: mergeNested(defaults.barangay_staff, saved.barangay_staff || {})
  };
}

function cloneSettings(value) {
  return JSON.parse(JSON.stringify(value || {}));
}

function sameSettings(left, right) {
  return JSON.stringify(left ?? null) === JSON.stringify(right ?? null);
}

function mergeNested(defaultValue, savedValue) {
  if (!defaultValue || typeof defaultValue !== "object" || Array.isArray(defaultValue)) {
    return savedValue === undefined ? defaultValue : savedValue;
  }

  return Object.entries(defaultValue).reduce((merged, [key, value]) => {
    merged[key] = mergeNested(value, savedValue?.[key]);
    return merged;
  }, {});
}

function getNestedValue(source, path) {
  return path.split(".").reduce((value, key) => value?.[key], source);
}

function setNestedValue(source, path, value) {
  const [key, ...rest] = path.split(".");
  if (!rest.length) {
    return { ...(source || {}), [key]: value };
  }

  return {
    ...(source || {}),
    [key]: setNestedValue(source?.[key] || {}, rest.join("."), value)
  };
}
