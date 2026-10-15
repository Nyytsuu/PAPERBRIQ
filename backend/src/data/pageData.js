const pageData = {
  dashboard: {
    systemStatus: {
      status: "Running",
      message: "All PaperBriq systems are operating smoothly.",
      rainwaterLevel: 78,
      batteryLevel: 92,
      temperatureC: 29.4,
      humidityPercent: 65,
      powerOutputWatts: 240
    },
    processProgress: [
      { step: "Collect Paper", progress: 100, status: "completed", detail: "Collected: 45 kg / 60 kg" },
      { step: "Shred Paper", progress: 78, status: "in_progress", detail: "Shredding at 78%" },
      { step: "Pulp and Mix", progress: 60, status: "in_progress", detail: "Mixture hydration 60%" },
      { step: "Press and Mold", progress: 0, status: "not_started", detail: "0 / 20 briquettes" },
      { step: "Drying", progress: 0, status: "not_started", detail: "Waiting to start" }
    ],
    processLog: [
      { time: "10:35 AM", event: "Shredding Paper Started", type: "Process" },
      { time: "10:50 AM", event: "Rainwater Added", type: "Resource" },
      { time: "11:15 AM", event: "Pulping Mixture Began", type: "Process" },
      { time: "11:45 AM", event: "Paper Collection Completed", type: "Completed" },
      { time: "12:03 PM", event: "System Check OK", type: "System" }
    ],
    weeklySummary: {
      paperWasteKg: 45,
      briquettesProduced: 8,
      briquetteTarget: 20,
      waterSavedL: 120,
      activeContributors: 15
    }
  },
  process: {
    activeBatch: {
      batchCode: "PBQ-2026-0701-A",
      status: "in_progress",
      overallProgress: 65,
      estimatedTimeRemaining: "1 hr 20 mins",
      targetBriquettes: 20,
      completedBriquettes: 8
    },
    steps: [
      { name: "Collect Paper", progress: 100, status: "completed", detail: "45 kg / 60 kg" },
      { name: "Shred Paper", progress: 78, status: "in_progress", detail: "Shredding at 78%" },
      { name: "Pulp and Mix", progress: 60, status: "in_progress", detail: "Hydration at 60%" },
      { name: "Press and Mold", progress: 0, status: "not_started", detail: "0 / 20 briquettes" },
      { name: "Drying", progress: 0, status: "not_started", detail: "Waiting to start" }
    ],
    qualityChecks: [
      { label: "Moisture", value: "8.5% optimal" },
      { label: "Mold Pressure", value: "2.3 bar" },
      { label: "Drying Temperature", value: "55 C" },
      { label: "Safety Interlock", value: "Locked" }
    ],
    logs: [
      { time: "10:35 AM", event: "Shredding Paper Started", status: "Process" },
      { time: "10:50 AM", event: "Rainwater Added", status: "Resource" },
      { time: "11:15 AM", event: "Pulping Mixture Began", status: "Process" },
      { time: "11:45 AM", event: "Paper Collection Completed", status: "Completed" }
    ]
  },
  iot: {
    sensors: [
      { name: "Temperature", value: 29.4, unit: "C", status: "Normal" },
      { name: "Humidity", value: 65, unit: "%RH", status: "Normal" },
      { name: "Rainwater Level", value: 78, unit: "%", status: "Normal" },
      { name: "Battery Level", value: 92, unit: "%", status: "Good" },
      { name: "Power Output", value: 240, unit: "w", status: "Normal" },
      { name: "Water Flow Rate", value: 18.6, unit: "L/min", status: "Normal" },
      { name: "System Pressure", value: 2.3, unit: "bar", status: "Normal" },
      { name: "TDS Water Quality", value: 152, unit: "ppm", status: "Good" }
    ],
    devices: [
      { name: "Motor Status", value: "ON", status: "Running" },
      { name: "Pump Status", value: "ON", status: "Running" },
      { name: "Heater Status", value: "OFF", status: "Standby" },
      { name: "Fan Status", value: "ON", status: "Running" },
      { name: "Safety Interlock", value: "LOCKED", status: "Secure" }
    ],
    alerts: [
      { title: "Low Rainwater Level", severity: "Warning", message: "Rainwater level is below 80% threshold." },
      { title: "High Motor Temperature", severity: "Warning", message: "Motor temperature is above normal range." },
      { title: "Scheduled Maintenance", severity: "Info", message: "Filter cleaning scheduled today at 2:00 PM." },
      { title: "System Check OK", severity: "Info", message: "All systems operating within normal parameters." }
    ],
    diagnostics: [
      { item: "Sensor Connectivity", status: "OK" },
      { item: "Data Transmission", status: "OK" },
      { item: "Edge Controller", status: "OK" },
      { item: "Cloud Sync", status: "OK" },
      { item: "Power Supply", status: "OK" },
      { item: "Backup Power", status: "OK" }
    ],
    network: {
      onlineDevices: "12 / 12",
      packetSuccess: "99.6%",
      responseTimeMs: 320,
      lastDataSync: "10:35 AM"
    }
  },
  contributions: {
    metrics: {
      totalContributors: 15,
      paperDonatedThisWeekKg: 45,
      briquettesProducedThisWeek: 8,
      weeklyTarget: 20
    },
    contributors: [
      { name: "Juan Dela Cruz", barangay: "San Isidro", paperDonatedKg: 5, points: 50, status: "Scanned" },
      { name: "Maria Santos", barangay: "Newhope", paperDonatedKg: 4.5, points: 45, status: "Scanned" },
      { name: "Pedro Reyes", barangay: "San Isidro", paperDonatedKg: 6, points: 60, status: "Scanned" },
      { name: "Ana Lopez", barangay: "Del Pilar", paperDonatedKg: 3, points: 30, status: "Scanned" },
      { name: "Mark Villanueva", barangay: "Newhope", paperDonatedKg: 2.5, points: 25, status: "Scanned" },
      { name: "Liza Garcia", barangay: "San Isidro", paperDonatedKg: 4, points: 40, status: "Scanned" },
      { name: "Ramon Bautista", barangay: "Del Pilar", paperDonatedKg: 1.5, points: 15, status: "Pending" },
      { name: "Karla Mendoza", barangay: "Newhope", paperDonatedKg: 3.5, points: 35, status: "Scanned" },
      { name: "Diego Cruz", barangay: "San Isidro", paperDonatedKg: 2, points: 20, status: "Scanned" },
      { name: "Faith Domingo", barangay: "Del Pilar", paperDonatedKg: 4.5, points: 45, status: "Scanned" }
    ]
  },
  rewards: {
    totals: {
      totalPoints: 320,
      pointsEarnedThisMonth: 320,
      pointsRedeemedThisMonth: 120,
      availablePoints: 320,
      currentStreakDays: 7
    },
    history: [
      { points: 45, type: "earned", title: "Paper Collection", happenedAt: "May 12, 2026 11:45 AM" },
      { points: 30, type: "earned", title: "Pulping Mixture Contribution", happenedAt: "May 10, 2026 10:15 AM" },
      { points: 25, type: "earned", title: "System Check Bonus", happenedAt: "May 8, 2026 09:30 AM" },
      { points: -60, type: "redeemed", title: "Eco Store Voucher P100", happenedAt: "May 5, 2026 04:20 PM" }
    ],
    nextTier: {
      name: "Silver Recycler",
      targetPoints: 500,
      currentPoints: 320,
      benefits: ["5% Bonus Points", "Priority in Rewards", "Exclusive PaperBriq Badges"]
    },
    rewards: [
      { id: "voucher-100", name: "Eco Store Voucher P100", pointsCost: 100, category: "Vouchers", description: "Local eco-store voucher for approved contributor claims.", stockQuantity: 18, imageUrl: "" },
      { id: "voucher-250", name: "Eco Store Voucher P250", pointsCost: 250, category: "Vouchers", description: "Higher value eco-store voucher for regular recyclers.", stockQuantity: 10, imageUrl: "" },
      { id: "tote-bag", name: "Eco Tote Bag", pointsCost: 300, category: "Eco Products", description: "Reusable PaperBriq tote bag.", stockQuantity: 24, imageUrl: "" },
      { id: "plant-tree", name: "Plant a Tree", pointsCost: 350, category: "Green Donations", description: "Tree planting certificate sponsored by the barangay.", stockQuantity: 40, imageUrl: "" },
      { id: "water-bottle", name: "Eco Water Bottle", pointsCost: 400, category: "Eco Products", description: "Reusable bottle for active contributors.", stockQuantity: 14, imageUrl: "" }
    ]
  },
  reports: {
    period: "May 01 - May 31, 2026",
    cards: [
      { label: "Paper Collected", value: "1,245 kg", change: "+18.6%" },
      { label: "Briquettes Produced", value: "248 / 620", change: "+12.4%" },
      { label: "Water Saved", value: "3,120 L", change: "+15.2%" },
      { label: "Active Contributors", value: "86", change: "+10.3%" }
    ],
    charts: {
      paperCollected: [780, 1050, 920, 1280, 1180],
      briquettesProduced: [180, 210, 250, 170, 252],
      waterSaved: [2000, 2400, 2920, 3100, 2960]
    },
    impact: [
      { label: "Trees Saved", value: "18" },
      { label: "CO2 Reduced", value: "312 kg" },
      { label: "Energy Generated", value: "240 kWh" },
      { label: "Landfill Diversion", value: "1,245 kg" }
    ]
  },
  history: {
    stats: {
      totalSessions: 78,
      completedRuns: 64,
      successRate: "82%",
      totalRuntime: "1 hr 20 mins"
    },
    events: [
      { time: "12:03 PM", title: "System Check OK", description: "All systems are operating normally.", category: "System" },
      { time: "11:45 AM", title: "Paper Collection Completed", description: "Collected 45 kg / 60 kg.", category: "Process Run" },
      { time: "11:15 AM", title: "Pulping Mixture Began", description: "Pulping mixture process started.", category: "Process Run" },
      { time: "10:50 AM", title: "Rainwater Added", description: "Added 120 L of rainwater.", category: "Resource" },
      { time: "09:30 AM", title: "QR Code Scanned", description: "User: Juan Dela Cruz. Points earned: +20 pts.", category: "QR Scan" }
    ]
  },
  settings: {
    userSettings: {
      profile: {
        fullName: "Juan Dela Cruz",
        contactNumber: "+63 912 345 6789",
        email: "juan@example.com",
        barangay: "San Isidro",
        address: "",
        profilePicture: "Use initials"
      },
      qr: {
        code: "PBQ-USER-juan-delacruz",
        showForScanning: true,
        allowDownload: true,
        active: true
      },
      notifications: {
        pointsEarned: true,
        rewardRedeemed: true,
        newRewardAvailable: true,
        collectionSchedule: true,
        impactUpdates: true,
        weeklySummary: false
      },
      rewardPreferences: {
        schoolSupplies: true,
        ecoProducts: true,
        vouchers: true,
        foodPacks: false,
        plantingActivities: true,
        communityCertificates: true
      },
      contribution: {
        defaultDropoff: "Barangay San Isidro",
        collectionReminder: "Every Friday",
        showHistory: true
      },
      privacy: {
        showOnLeaderboard: true,
        allowStaffQrProfile: true,
        loginActivity: true
      },
      appPreferences: {
        theme: "Light",
        language: "English",
        fontSize: "Medium",
        dateFormat: "MM/DD/YYYY"
      }
    },
    profile: {
      fullName: "Juan Dela Cruz",
      email: "juan.delacruz@example.com",
      phone: "+63 912 345 6789",
      role: "Contributor"
    },
    account: {
      username: "juan.delacruz",
      accountType: "Contributor",
      memberSince: "May 15, 2024",
      timeZone: "(GMT+8) Asia/Manila"
    },
    machine: {
      defaultBriquetteSize: "Standard (42x22 cm)",
      autoStartMachine: true,
      shredLevel: "Medium",
      dryingTemperature: "55 C"
    },
    notifications: {
      email: true,
      sms: true,
      processAlerts: true,
      maintenanceReminders: true,
      weeklySummary: true
    },
    qr: {
      display: true,
      autoRefresh: false,
      expiry: "24 Hours",
      allowDownload: true
    },
    appearance: {
      theme: "Light",
      language: "English (US)",
      dateFormat: "MM/DD/YYYY"
    }
  },
  qrScans: {
    scanStatus: {
      status: "success",
      message: "Contributor verified and data recorded."
    },
    contributor: {
      name: "Maria Santos",
      barangay: "San Isidro",
      memberSince: "March 15, 2024",
      totalContributions: 12,
      pointsEarned: 15
    },
    tips: [
      "Ensure the QR code is clear and well-lit.",
      "Hold your device steady.",
      "Avoid glare or shadows on the code."
    ],
    recent: [
      { time: "10:35 AM", contributor: "Maria Santos", barangay: "San Isidro", weightKg: 12.5, points: 15, status: "Success" },
      { time: "10:20 AM", contributor: "Pedro Dela Cruz", barangay: "San Rafael", weightKg: 8, points: 10, status: "Success" },
      { time: "10:05 AM", contributor: "Ana Reyes", barangay: "San Isidro", weightKg: 15, points: 18, status: "Success" },
      { time: "09:50 AM", contributor: "Luis Gonzales", barangay: "San Roque", weightKg: 6, points: 7, status: "Success" },
      { time: "09:35 AM", contributor: "Carmen Villanueva", barangay: "San Isidro", weightKg: 10, points: 12, status: "Success" }
    ]
  }
};

module.exports = { pageData };
