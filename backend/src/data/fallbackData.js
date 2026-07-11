const fallbackUsers = [
  {
    id: "demo-admin",
    fullName: "PaperBriq Admin",
    email: "admin@paperbriq.local",
    password: "paperbriq123",
    role: "admin",
    barangay: "Central"
  },
  {
    id: "demo-staff",
    fullName: "Barangay Staff Demo",
    email: "staff@paperbriq.local",
    password: "paperbriq123",
    role: "barangay_staff",
    barangay: "San Isidro"
  },
  {
    id: "demo-contributor",
    fullName: "Contributor Demo",
    email: "contributor@paperbriq.local",
    password: "paperbriq123",
    role: "contributor",
    barangay: "San Isidro"
  }
];

const fallbackSummary = {
  totals: {
    paperKg: 457.5,
    briquettes: 1081,
    contributors: 38,
    co2KgAvoided: 374.55
  },
  collections: [
    { barangay: "San Isidro", paperWeightKg: 26, status: "sorted", collectedAt: "Today, 8:20 AM" },
    { barangay: "Central", paperWeightKg: 18.5, status: "received", collectedAt: "Today, 10:10 AM" },
    { barangay: "Mabini", paperWeightKg: 12.75, status: "queued", collectedAt: "Yesterday" }
  ],
  batches: [
    { batchCode: "PBQ-2026-0701-A", briquettesProduced: 410, qualityStatus: "ready" },
    { batchCode: "PBQ-2026-0630-B", briquettesProduced: 316, qualityStatus: "curing" }
  ]
};

module.exports = {
  fallbackSummary,
  fallbackUsers
};
