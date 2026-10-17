const { randomUUID } = require("crypto");
const { ROLE_LABELS } = require("../config/roles");
const { fallbackSummary, fallbackUsers } = require("../data/fallbackData");
const { createLiveDashboardSnapshot, createLiveSummarySnapshot } = require("../data/liveDashboard");
const { createLiveIotSnapshot } = require("../data/liveIot");
const { createLiveReportsSnapshot } = require("../data/liveReports");
const { pageData } = require("../data/pageData");
const {
  canAcknowledge,
  inferProcessState,
  mapDeviceCommand,
  mapDeviceEvent,
  publicStoreError
} = require("./deviceControl");

function createFallbackStore() {
  const deviceCommands = [];
  const deviceEvents = [];
  const devicePresence = new Map();

  function touchDevice(deviceId) {
    devicePresence.set(deviceId, new Date().toISOString());
  }

  function expireCommands(now = new Date()) {
    for (const command of deviceCommands) {
      if (command.status === "queued" && new Date(command.expiresAt) <= now) {
        command.status = "expired";
        command.completedAt = now.toISOString();
        command.message = command.message || "Command expired before device acknowledgement.";
      }
    }
  }

  return {
    mode: "demo memory",
    configured: false,
    async close() {},
    async login(email, password, role) {
      const user = fallbackUsers.find((candidate) => (
        candidate.email === email &&
        candidate.password === password &&
        (!role || candidate.role === role)
      ));

      if (!user) return null;

      return {
        id: user.id,
        fullName: user.fullName,
        email: user.email,
        role: user.role,
        roleLabel: ROLE_LABELS[user.role],
        barangay: user.barangay,
        profileImageUrl: user.profileImageUrl || ""
      };
    },
    async signup(signup) {
      const exists = fallbackUsers.some((candidate) => candidate.email === signup.email);
      if (exists) {
        const error = new Error("Duplicate demo account");
        error.statusCode = 409;
        error.publicMessage = "An account with that email already exists.";
        throw error;
      }

      const user = {
        id: `demo-${randomUUID()}`,
        fullName: signup.fullName,
        email: signup.email,
        password: signup.password,
        role: signup.role,
        barangay: signup.barangay
      };
      fallbackUsers.push(user);

      return {
        id: user.id,
        fullName: user.fullName,
        email: user.email,
        role: user.role,
        roleLabel: ROLE_LABELS[user.role],
        barangay: user.barangay,
        profileImageUrl: user.profileImageUrl || ""
      };
    },
    async getStaffAccounts() {
      return fallbackUsers
        .filter((user) => user.role === "barangay_staff")
        .map((user) => ({
          id: user.id,
          fullName: user.fullName,
          email: user.email,
          role: user.role,
          roleLabel: ROLE_LABELS[user.role],
          barangay: user.barangay,
          profileImageUrl: user.profileImageUrl || "",
          isActive: true,
          createdAt: null
        }));
    },
    async createStaffAccount(staff) {
      const exists = fallbackUsers.some((candidate) => candidate.email === staff.email);
      if (exists) {
        const error = new Error("Duplicate demo account");
        error.statusCode = 409;
        error.publicMessage = "An account with that email already exists.";
        throw error;
      }

      const user = {
        id: `demo-${randomUUID()}`,
        fullName: staff.fullName,
        email: staff.email,
        password: staff.password,
        role: "barangay_staff",
        barangay: staff.barangay
      };
      fallbackUsers.unshift(user);

      return {
        id: user.id,
        fullName: user.fullName,
        email: user.email,
        role: user.role,
        roleLabel: ROLE_LABELS[user.role],
        barangay: user.barangay,
        profileImageUrl: user.profileImageUrl || "",
        isActive: true,
        createdAt: new Date().toISOString()
      };
    },
    async updateProfileImage(profile) {
      const user = fallbackUsers.find((candidate) => (
        candidate.email === profile.email &&
        candidate.role === profile.role
      ));

      if (!user) {
        const error = new Error("Staff account not found.");
        error.statusCode = 404;
        error.publicMessage = "Staff account not found.";
        throw error;
      }

      user.profileImageUrl = profile.imageData;
      return {
        id: user.id,
        fullName: user.fullName,
        email: user.email,
        role: user.role,
        roleLabel: ROLE_LABELS[user.role],
        barangay: user.barangay,
        profileImageUrl: user.profileImageUrl || ""
      };
    },
    async getSummary() {
      return createLiveSummarySnapshot(fallbackSummary);
    },
    async getDashboard() {
      return createLiveDashboardSnapshot(pageData.dashboard, createLiveSummarySnapshot(fallbackSummary), pageData.iot);
    },
    async getProcess() {
      const dashboard = createLiveDashboardSnapshot(pageData.dashboard, createLiveSummarySnapshot(fallbackSummary), pageData.iot);

      return {
        ...pageData.process,
        activeBatch: {
          ...pageData.process.activeBatch,
          overallProgress: dashboard.overallProgress,
          estimatedTimeRemaining: dashboard.overallProgress > 82 ? "35 mins" : "1 hr 10 mins",
          completedBriquettes: dashboard.weeklySummary.briquettesProduced
        },
        steps: dashboard.processProgress.map((step) => ({
          name: step.step,
          progress: step.progress,
          status: step.status,
          detail: step.detail
        })),
        logs: dashboard.processLog.map((log) => ({
          time: log.time,
          event: log.event,
          status: log.type
        }))
      };
    },
    async createProcessAction(action) {
      return {
        id: `demo-${randomUUID()}`,
        action: action.action || "start",
        status: "accepted",
        created_at: new Date().toISOString()
      };
    },
    async createDeviceCommand(input) {
      expireCommands();
      if (input.idempotencyKey) {
        const existing = deviceCommands.find((command) => (
          command.deviceId === input.deviceId && command.idempotencyKey === input.idempotencyKey
        ));
        if (existing) return mapDeviceCommand(existing);
      }

      const createdAt = new Date();
      const command = {
        id: randomUUID(),
        deviceId: input.deviceId,
        command: input.command,
        status: "queued",
        idempotencyKey: input.idempotencyKey || null,
        createdAt: createdAt.toISOString(),
        expiresAt: new Date(createdAt.getTime() + input.ttlSeconds * 1000).toISOString(),
        deliveredAt: null,
        completedAt: null,
        message: ""
      };
      if (input.command === "stop") {
        for (const pending of deviceCommands) {
          if (pending.deviceId === input.deviceId && pending.status === "queued") {
            pending.status = "expired";
            pending.completedAt = createdAt.toISOString();
            pending.message = "Superseded by a newer stop command.";
          }
        }
      }
      deviceCommands.unshift(command);
      return mapDeviceCommand(command);
    },
    async getNextDeviceCommand(deviceId) {
      const now = new Date();
      touchDevice(deviceId);
      expireCommands(now);
      const command = [...deviceCommands]
        .reverse()
        .find((candidate) => candidate.deviceId === deviceId && candidate.status === "queued");
      return mapDeviceCommand(command);
    },
    async acknowledgeDeviceCommand(id, acknowledgement) {
      const now = new Date();
      touchDevice(acknowledgement.deviceId);
      expireCommands(now);
      const command = deviceCommands.find((candidate) => (
        candidate.id === id && candidate.deviceId === acknowledgement.deviceId
      ));
      if (!command) throw publicStoreError(404, "Device command not found.");
      if (!canAcknowledge(command.status, acknowledgement.status)) {
        throw publicStoreError(409, `Command cannot move from ${command.status} to ${acknowledgement.status}.`);
      }
      if (command.status === acknowledgement.status) return mapDeviceCommand(command);

      command.status = acknowledgement.status;
      command.message = acknowledgement.message || command.message;
      if (acknowledgement.status === "delivered") command.deliveredAt = now.toISOString();
      if (["completed", "failed"].includes(acknowledgement.status)) {
        command.deliveredAt ||= now.toISOString();
        command.completedAt = now.toISOString();
      }
      return mapDeviceCommand(command);
    },
    async recordDeviceEvent(input) {
      touchDevice(input.deviceId);
      if (input.eventKey) {
        const existing = deviceEvents.find((event) => (
          event.deviceId === input.deviceId && event.eventKey === input.eventKey
        ));
        if (existing) return mapDeviceEvent(existing);
      }

      const event = {
        id: randomUUID(),
        deviceId: input.deviceId,
        eventKey: input.eventKey || null,
        type: input.type,
        severity: input.severity,
        component: input.component,
        message: input.message,
        processState: input.processState || null,
        commandId: input.commandId || null,
        metadata: input.metadata || {},
        createdAt: new Date().toISOString()
      };
      deviceEvents.unshift(event);
      return mapDeviceEvent(event);
    },
    async getDeviceControlStatus(deviceId, onlineAfter) {
      expireCommands();
      const commands = deviceCommands
        .filter((command) => command.deviceId === deviceId)
        .slice(0, 12)
        .map(mapDeviceCommand);
      const events = deviceEvents
        .filter((event) => event.deviceId === deviceId)
        .slice(0, 20)
        .map(mapDeviceEvent);
      const lastSeenAt = devicePresence.get(deviceId) || null;
      return {
        deviceId,
        processState: inferProcessState(events, commands),
        device: {
          connected: Boolean(lastSeenAt && new Date(lastSeenAt) >= onlineAfter),
          lastSeenAt,
          lastEventAt: events[0]?.createdAt || null
        },
        commands,
        events
      };
    },
    async getIotMonitoring() {
      return createLiveIotSnapshot(pageData.iot);
    },
    async recordIotTelemetry(telemetry) {
      const now = new Date().toISOString();
      for (const reading of telemetry.readings) {
        const current = pageData.iot.sensors.find((sensor) => sensor.name === reading.name);
        const next = {
          name: reading.name,
          value: reading.value,
          unit: reading.unit,
          status: reading.status,
          source: "esp32",
          updatedAt: now
        };
        if (current) Object.assign(current, next);
        else pageData.iot.sensors.push(next);
      }
      pageData.iot.diagnostics = mergeHardwareDiagnostics(pageData.iot.diagnostics, telemetry.hardware);
      return { deviceId: telemetry.deviceId, readingsStored: telemetry.readings.length, recordedAt: now };
    },
    async getContributions() {
      return pageData.contributions;
    },
    async getRewards() {
      return buildRewardMonitor(pageData.rewards);
    },
    async createRewardItem(reward) {
      const item = {
        id: reward.id || createRewardCode(reward.name),
        name: reward.name,
        pointsCost: Number(reward.pointsCost || 0),
        category: reward.category || "Barangay Reward",
        description: reward.description || "",
        stockQuantity: Number(reward.stockQuantity || 0),
        imageUrl: reward.imageUrl || "",
        createdByRole: reward.creatorRole || "barangay_staff",
        createdByName: reward.creatorName || "Barangay Staff",
        createdAt: new Date().toISOString()
      };
      pageData.rewards.rewards.unshift(item);

      return item;
    },
    async redeemReward(request) {
      const reward = pageData.rewards.rewards.find((item) => item.id === request.rewardId);
      if (!reward) {
        const error = new Error("Reward item not found.");
        error.statusCode = 404;
        error.publicMessage = "Reward item not found.";
        throw error;
      }
      if (Number(reward.stockQuantity || 0) <= 0) {
        const error = new Error("Reward item is out of stock.");
        error.statusCode = 409;
        error.publicMessage = "Reward item is out of stock.";
        throw error;
      }
      reward.stockQuantity = Math.max(0, Number(reward.stockQuantity || 0) - 1);
      pageData.rewards.history.unshift({
        points: -Math.abs(Number(reward.pointsCost || 0)),
        type: "redeemed",
        title: `Redeemed reward - ${reward.name}`,
        happenedAt: new Intl.DateTimeFormat("en-PH", {
          month: "short",
          day: "2-digit",
          year: "numeric",
          hour: "2-digit",
          minute: "2-digit"
        }).format(new Date())
      });
      return {
        id: `demo-${randomUUID()}`,
        rewardId: request.rewardId,
        rewardName: reward.name,
        pointsSpent: Number(reward.pointsCost || 0),
        status: "redeemed",
        created_at: new Date().toISOString()
      };
    },
    async getReports() {
      return createLiveReportsSnapshot(pageData.reports, createLiveSummarySnapshot(fallbackSummary));
    },
    async getHistory() {
      return pageData.history;
    },
    async getSettings() {
      return pageData.settings;
    },
    async updateSettings(settings) {
      return {
        settings: { ...pageData.settings, ...settings },
        updated_at: new Date().toISOString()
      };
    },
    async getQrScans() {
      return pageData.qrScans;
    },
    async createQrScan(scan) {
      const weightKg = Number(scan.weightKg || 0);
      if (!String(scan.code || "").trim() || !Number.isFinite(weightKg) || weightKg <= 0) {
        const error = new Error("Contributor QR code and paper weight are required.");
        error.statusCode = 400;
        error.publicMessage = "Contributor QR code and paper weight are required.";
        throw error;
      }

      const contributor = String(scan.contributor || scan.code).trim();
      const barangay = String(scan.barangay || "Unassigned").trim();
      const pointsEarned = Math.round(weightKg * 10);
      const item = {
        time: new Intl.DateTimeFormat("en-PH", {
          hour: "2-digit",
          minute: "2-digit"
        }).format(new Date()),
        contributor,
        barangay,
        weightKg,
        points: pointsEarned,
        status: "Success"
      };
      pageData.qrScans.recent.unshift(item);
      pageData.qrScans.recent = pageData.qrScans.recent.slice(0, 20);
      pageData.qrScans.scanStatus = {
        status: "success",
        message: `${contributor} scan recorded.`
      };
      pageData.qrScans.contributor = {
        name: contributor,
        barangay,
        memberSince: "",
        totalContributions: pageData.qrScans.recent.filter((entry) => entry.contributor === contributor).length,
        pointsEarned
      };

      return {
        id: `demo-${randomUUID()}`,
        code: scan.code || "manual",
        status: "success",
        pointsEarned,
        created_at: new Date().toISOString()
      };
    },
    async createAccessRequest() {
      return {
        id: `demo-${randomUUID()}`,
        status: "pending",
        created_at: new Date().toISOString()
      };
    }
  };
}

function mergeHardwareDiagnostics(diagnostics, hardware) {
  const labels = {
    dht22: "DHT22",
    irSensor: "IR Sensor",
    tftDisplay: "TFT Display",
    floatSwitch: "Float Switch"
  };
  const result = [...(diagnostics || [])];
  for (const [key, label] of Object.entries(labels)) {
    if (!(key in hardware)) continue;
    const existing = result.find((item) => item.item === label);
    const status = String(hardware[key] || "Unknown");
    if (existing) existing.status = status;
    else result.push({ item: label, status });
  }
  return result;
}

function buildRewardMonitor(rewardsData) {
  const rewards = rewardsData.rewards || [];
  const history = rewardsData.history || [];
  const pointsRedeemed = history
    .filter((item) => item.type === "redeemed" || Number(item.points) < 0)
    .reduce((sum, item) => sum + Math.abs(Number(item.points || 0)), 0);

  return {
    ...rewardsData,
    monitor: {
      rewardItems: rewards.length,
      activeItems: rewards.length,
      totalStock: rewards.reduce((sum, item) => sum + Number(item.stockQuantity || 0), 0),
      pointsRedeemed,
      pendingClaims: 3,
      lowStockItems: rewards.filter((item) => Number(item.stockQuantity || 0) <= 5).length
    }
  };
}

function createRewardCode(name) {
  const slug = String(name || "reward")
    .trim()
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, "-")
    .replace(/^-|-$/g, "")
    .slice(0, 48) || "reward";
  return `${slug}-${randomUUID().slice(0, 8)}`;
}

module.exports = { createFallbackStore };
